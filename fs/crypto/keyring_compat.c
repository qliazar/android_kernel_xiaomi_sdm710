// SPDX-License-Identifier: GPL-2.0
/*
 * fscrypt keyring ioctls for a 4.9 kernel (pyxis HyperOS port).
 *
 * Android 15 vold only installs file-encryption keys with the filesystem
 * keyring ioctls introduced in Linux 5.4 (FS_IOC_ADD_ENCRYPTION_KEY & co.).
 * This 4.9 tree only knows v1 policies whose master keys are looked up in
 * the process keyrings. Here v1 master keys added through the new ioctls are
 * kept in a small in-kernel table that find_and_derive_key() consults when
 * no process-keyring key exists. Only descriptor (v1) key specifiers are
 * supported; v2 policies return -EOPNOTSUPP.
 */
#include <linux/capability.h>
#include <linux/fscrypt.h>
#include <linux/list.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include "fscrypt_private.h"

struct compat_key {
	struct list_head node;
	u8 descriptor[FS_KEY_DESCRIPTOR_SIZE];
	struct fscrypt_key key;
};

static LIST_HEAD(compat_keys);
static DECLARE_RWSEM(compat_keys_sem);

static struct compat_key *lookup(const u8 *descriptor)
{
	struct compat_key *k;

	list_for_each_entry(k, &compat_keys, node)
		if (!memcmp(k->descriptor, descriptor, FS_KEY_DESCRIPTOR_SIZE))
			return k;
	return NULL;
}

int fscrypt_compat_get_key(const u8 *descriptor, unsigned int min_keysize,
			   struct fscrypt_key *out)
{
	struct compat_key *k;
	int err = -ENOKEY;

	down_read(&compat_keys_sem);
	k = lookup(descriptor);
	if (k && k->key.size >= min_keysize) {
		memcpy(out, &k->key, sizeof(*out));
		err = 0;
	}
	up_read(&compat_keys_sem);
	return err;
}

static int check_spec(const struct fscrypt_key_specifier *spec)
{
	if (spec->__reserved)
		return -EINVAL;
	if (spec->type == FSCRYPT_KEY_SPEC_TYPE_IDENTIFIER)
		return -EOPNOTSUPP;
	if (spec->type != FSCRYPT_KEY_SPEC_TYPE_DESCRIPTOR)
		return -EINVAL;
	return 0;
}

static int add_key(void __user *uarg)
{
	struct fscrypt_add_key_arg arg;
	struct compat_key *k, *old;
	int err;

	if (copy_from_user(&arg, uarg, sizeof(arg)))
		return -EFAULT;
	err = check_spec(&arg.key_spec);
	if (err)
		return err;
	if (arg.key_id || memchr_inv(arg.__reserved, 0, sizeof(arg.__reserved)))
		return -EINVAL;
	if (arg.raw_size < 16 || arg.raw_size > FS_MAX_KEY_SIZE)
		return -EINVAL;
	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	k = kzalloc(sizeof(*k), GFP_KERNEL);
	if (!k)
		return -ENOMEM;
	memcpy(k->descriptor, arg.key_spec.u.descriptor, FS_KEY_DESCRIPTOR_SIZE);
	k->key.mode = 0;
	k->key.size = arg.raw_size;
	if (copy_from_user(k->key.raw, (u8 __user *)uarg + sizeof(arg), arg.raw_size)) {
		kzfree(k);
		return -EFAULT;
	}

	down_write(&compat_keys_sem);
	old = lookup(k->descriptor);
	if (old) {
		list_del(&old->node);
		kzfree(old);
	}
	list_add(&k->node, &compat_keys);
	up_write(&compat_keys_sem);
	return 0;
}

static int remove_key(void __user *uarg)
{
	struct fscrypt_remove_key_arg arg;
	struct compat_key *k;
	int err;

	if (copy_from_user(&arg, uarg, sizeof(arg)))
		return -EFAULT;
	err = check_spec(&arg.key_spec);
	if (err)
		return err;
	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	down_write(&compat_keys_sem);
	k = lookup(arg.key_spec.u.descriptor);
	if (k)
		list_del(&k->node);
	up_write(&compat_keys_sem);
	if (!k)
		return -ENOKEY;
	kzfree(k);
	/* already-open inodes keep their derived keys, as with 4.9 keyrings */
	arg.removal_status_flags = 0;
	return copy_to_user(uarg, &arg, sizeof(arg)) ? -EFAULT : 0;
}

static int key_status(void __user *uarg)
{
	struct fscrypt_get_key_status_arg arg;
	bool present;
	int err;

	if (copy_from_user(&arg, uarg, sizeof(arg)))
		return -EFAULT;
	err = check_spec(&arg.key_spec);
	if (err)
		return err;

	down_read(&compat_keys_sem);
	present = lookup(arg.key_spec.u.descriptor) != NULL;
	up_read(&compat_keys_sem);
	arg.status = present ? FSCRYPT_KEY_STATUS_PRESENT : FSCRYPT_KEY_STATUS_ABSENT;
	arg.status_flags = 0;
	arg.user_count = 0;
	return copy_to_user(uarg, &arg, sizeof(arg)) ? -EFAULT : 0;
}

/* v1 policies only: struct fscrypt_policy (4.9) has the fscrypt_policy_v1 layout */
static int get_policy_ex(struct file *filp, void __user *uarg)
{
	u64 size;
	int err;

	if (get_user(size, (u64 __user *)uarg))
		return -EFAULT;
	if (size < sizeof(struct fscrypt_policy))
		return -EOVERFLOW;
	err = fscrypt_ioctl_get_policy(filp, (u8 __user *)uarg + sizeof(u64));
	if (err)
		return err;
	return put_user((u64)sizeof(struct fscrypt_policy), (u64 __user *)uarg);
}

long fscrypt_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	void __user *uarg = (void __user *)arg;

	switch (cmd) {
	case FS_IOC_ADD_ENCRYPTION_KEY:
		return add_key(uarg);
	case FS_IOC_REMOVE_ENCRYPTION_KEY:
	case FS_IOC_REMOVE_ENCRYPTION_KEY_ALL_USERS:
		return remove_key(uarg);
	case FS_IOC_GET_ENCRYPTION_KEY_STATUS:
		return key_status(uarg);
	case FS_IOC_GET_ENCRYPTION_POLICY_EX:
		return get_policy_ex(filp, uarg);
	}
	return -ENOIOCTLCMD;
}
EXPORT_SYMBOL(fscrypt_compat_ioctl);
