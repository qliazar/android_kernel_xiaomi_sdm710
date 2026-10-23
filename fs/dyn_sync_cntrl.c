/*
 * Dynamic sync control driver V2
 *
 * by andip71 (alias Lord Boeffla)
 *
 * V 2.2 for sdm845 pappschlumpf (Erik Müller)
 *
 * All credits for original implementation to faux123
 *
 */

#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/writeback.h>
#include <linux/suspend.h>
#include <linux/fb.h>
#include <linux/dyn_sync_cntrl.h>

extern struct atomic_notifier_head panic_notifier_list;

// fsync_mutex protects dyn_fsync_active during suspend / resume transitions
static DEFINE_MUTEX(fsync_mutex);

bool suspend_active __read_mostly = true;
bool dyn_fsync_active __read_mostly = DYN_FSYNC_ACTIVE_DEFAULT;

static struct notifier_block fb_notif;

void dyn_fsync_force_flush(void)
{
	sync_filesystems(0);
	sync_filesystems(1);
}
EXPORT_SYMBOL(dyn_fsync_force_flush);

static ssize_t dyn_fsync_active_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", (dyn_fsync_active ? 1 : 0));
}

static ssize_t dyn_fsync_active_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;

	if (sscanf(buf, "%u", &data) == 1) {
		if (data == 1) {
			pr_info("dynamic fsync: enabled\n");
			dyn_fsync_active = true;
		} else if (data == 0) {
			pr_info("dynamic fsync: disabled\n");
			dyn_fsync_active = false;
			dyn_fsync_force_flush();
		} else {
			pr_info("dynamic fsync: bad value: %u\n", data);
		}
	}

	return count;
}

static ssize_t dyn_fsync_version_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "version: %u.%u\n",
		DYN_FSYNC_VERSION_MAJOR,
		DYN_FSYNC_VERSION_MINOR);
}

static ssize_t dyn_fsync_suspend_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "suspend active: %u\n", suspend_active);
}

static int dyn_fsync_panic_event(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	suspend_active = false;
	dyn_fsync_force_flush();
	pr_warn("dynamic fsync: panic - force flush!\n");

	return NOTIFY_DONE;
}

static int dyn_fsync_notify_sys(struct notifier_block *this, unsigned long code,
				void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT || code == SYS_POWER_OFF) {
		suspend_active = false;
		dyn_fsync_force_flush();
		pr_warn("dynamic fsync: reboot - force flush!\n");
	}
	return NOTIFY_DONE;
}

static int fb_notifier_cb(struct notifier_block *nb, unsigned long action,
			  void *data)
{
	struct fb_event *ev = data;
	int *blank;

	if (action != FB_EARLY_EVENT_BLANK || !ev || !ev->data)
		return NOTIFY_OK;

	blank = ev->data;
	if (*blank == FB_BLANK_UNBLANK) {
		mutex_lock(&fsync_mutex);
		suspend_active = true;
		mutex_unlock(&fsync_mutex);
	} else {
		mutex_lock(&fsync_mutex);
		suspend_active = false;
		if (dyn_fsync_active)
			dyn_fsync_force_flush();
		mutex_unlock(&fsync_mutex);
	}

	return NOTIFY_OK;
}

static int dyn_fsync_pm_callback(struct notifier_block *nb,
				  unsigned long action, void *data)
{
	switch (action) {
	case PM_SUSPEND_PREPARE:
	case PM_HIBERNATION_PREPARE:
		mutex_lock(&fsync_mutex);
		suspend_active = false;
		if (dyn_fsync_active)
			dyn_fsync_force_flush();
		mutex_unlock(&fsync_mutex);
		break;
	case PM_POST_SUSPEND:
	case PM_POST_HIBERNATION:
		mutex_lock(&fsync_mutex);
		suspend_active = true;
		mutex_unlock(&fsync_mutex);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block dyn_fsync_pm_notifier = {
	.notifier_call = dyn_fsync_pm_callback,
	.priority = 0,
};

static struct notifier_block dyn_fsync_reboot_notifier = {
	.notifier_call = dyn_fsync_notify_sys,
};

static struct notifier_block dyn_fsync_panic_block = {
	.notifier_call = dyn_fsync_panic_event,
	.priority = INT_MAX,
};

static struct kobj_attribute dyn_fsync_active_attribute =
	__ATTR(Dyn_fsync_active, 0664,
		dyn_fsync_active_show,
		dyn_fsync_active_store);

static struct kobj_attribute dyn_fsync_version_attribute =
	__ATTR(Dyn_fsync_version, 0444, dyn_fsync_version_show, NULL);

static struct kobj_attribute dyn_fsync_suspend_attribute =
	__ATTR(Dyn_fsync_suspend, 0444, dyn_fsync_suspend_show, NULL);

static struct attribute *dyn_fsync_active_attrs[] = {
	&dyn_fsync_active_attribute.attr,
	&dyn_fsync_version_attribute.attr,
	&dyn_fsync_suspend_attribute.attr,
	NULL,
};

static struct attribute_group dyn_fsync_active_attr_group = {
	.attrs = dyn_fsync_active_attrs,
};

static struct kobject *dyn_fsync_kobj;

static int __init dyn_fsync_init(void)
{
	int ret;

	register_reboot_notifier(&dyn_fsync_reboot_notifier);
	register_pm_notifier(&dyn_fsync_pm_notifier);
	atomic_notifier_chain_register(&panic_notifier_list, &dyn_fsync_panic_block);

	dyn_fsync_kobj = kobject_create_and_add("dyn_fsync", kernel_kobj);
	if (!dyn_fsync_kobj) {
		pr_err("dynamic fsync: failed to create kobject\n");
		return -ENOMEM;
	}

	ret = sysfs_create_group(dyn_fsync_kobj, &dyn_fsync_active_attr_group);
	if (ret) {
		pr_err("dynamic fsync: failed to create sysfs group\n");
		kobject_put(dyn_fsync_kobj);
		return ret;
	}

	fb_notif.notifier_call = fb_notifier_cb;
	fb_register_client(&fb_notif);

	pr_info("dynamic fsync: driver initialized successfully\n");

	return 0;
}

static void __exit dyn_fsync_exit(void)
{
	unregister_reboot_notifier(&dyn_fsync_reboot_notifier);
	unregister_pm_notifier(&dyn_fsync_pm_notifier);
	atomic_notifier_chain_unregister(&panic_notifier_list, &dyn_fsync_panic_block);
	fb_unregister_client(&fb_notif);

	if (dyn_fsync_kobj)
		kobject_put(dyn_fsync_kobj);

	pr_info("dynamic fsync: driver unregistered\n");
}

module_init(dyn_fsync_init);
module_exit(dyn_fsync_exit);

MODULE_AUTHOR("andip71, faux123");
MODULE_DESCRIPTION("Dynamic Fsync Control Driver");
MODULE_LICENSE("GPL v2");
