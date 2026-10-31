// SPDX-License-Identifier: GPL-2.0
/*
 * mi_disp_compat: minimal compat endpoint for the Xiaomi /dev/mi_display/disp_feature node that
 * HyperOS libmisurfaceflinger (FodMonitorThread) opens at start-up. The pyxis LineageOS 4.9 kernel
 * has no such driver. This device
 *  - accepts the three ioctls MI-SF issues (0x8010440b, 0x400c4407, 0xc0304401) and answers success,
 *  - hands 16-byte event records {u32 id, u32 type, u32 len(16), u32 value} to read()/poll(),
 *  - lets a privileged user-space daemon (fodbridge) inject those records with write().
 * Event type 2 / value bit 0x4 makes MI-SF send extCmd(10,6) to the Goodix HAL ("FOD UI ready").
 */
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kfifo.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/mi_disp_compat.h>

#define MI_DISP_EVT_SZ 16
#define IOCTL_GET_INIT   0x8010440bU /* _IOR('D', 0x0b, 16) */
#define IOCTL_REG_EVENT  0x400c4407U /* _IOW('D', 0x07, 12) */
#define IOCTL_SET_STATUS 0xc0304401U /* _IOWR('D', 0x01, 48) */

static DECLARE_KFIFO(mi_evq, u8, 4096);
static DEFINE_MUTEX(mi_evq_lock);
static DECLARE_WAIT_QUEUE_HEAD(mi_evq_wait);
static u8 mi_last_status[48];
static DEFINE_MUTEX(mi_status_lock);

static ssize_t mi_disp_read(struct file *f, char __user *buf, size_t count, loff_t *pos)
{
	unsigned int copied = 0;
	int ret;
	size_t n = count - (count % MI_DISP_EVT_SZ);

	if (n < MI_DISP_EVT_SZ)
		return -EINVAL;
	for (;;) {
		if (!kfifo_is_empty(&mi_evq))
			break;
		if (f->f_flags & O_NONBLOCK)
			return -EAGAIN;
		ret = wait_event_interruptible(mi_evq_wait, !kfifo_is_empty(&mi_evq));
		if (ret)
			return ret;
	}
	if (mutex_lock_interruptible(&mi_evq_lock))
		return -ERESTARTSYS;
	n = min_t(size_t, n, kfifo_len(&mi_evq));
	n -= n % MI_DISP_EVT_SZ;
	ret = n ? kfifo_to_user(&mi_evq, buf, n, &copied) : 0;
	mutex_unlock(&mi_evq_lock);
	return ret ? ret : copied;
}

static ssize_t mi_disp_write(struct file *f, const char __user *buf, size_t count, loff_t *pos)
{
	unsigned int copied = 0;
	int ret;

	if (count % MI_DISP_EVT_SZ || !count || count > 1024)
		return -EINVAL;
	if (mutex_lock_interruptible(&mi_evq_lock))
		return -ERESTARTSYS;
	ret = kfifo_from_user(&mi_evq, buf, count, &copied);
	mutex_unlock(&mi_evq_lock);
	if (ret)
		return ret;
	wake_up_interruptible(&mi_evq_wait);
	return copied;
}

static unsigned int mi_disp_poll(struct file *f, poll_table *wait)
{
	unsigned int mask = 0;

	poll_wait(f, &mi_evq_wait, wait);
	if (!kfifo_is_empty(&mi_evq))
		mask |= POLLIN | POLLRDNORM;
	return mask;
}

static bool mi_fod_session;
void mi_disp_compat_fod_pressed(bool pressed);

static long mi_disp_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	u8 tmp[48];

	switch (cmd) {
	case IOCTL_GET_INIT: /* MI-SF reads u32 at +8 as the initial HBM overlay alpha: 0 = no dimming */
		memset(tmp, 0, 16);
		return copy_to_user((void __user *)arg, tmp, 16) ? -EFAULT : 0;
	case IOCTL_REG_EVENT:
		if (copy_from_user(tmp, (void __user *)arg, 12))
			return -EFAULT;
		pr_info("mi_disp_compat: register event id=%u\n", ((u32 *)tmp)[2]);
		return 0;
	case IOCTL_SET_STATUS:
		if (copy_from_user(tmp, (void __user *)arg, 48))
			return -EFAULT;
		mutex_lock(&mi_status_lock);
		memcpy(mi_last_status, tmp, 48);
		mutex_unlock(&mi_status_lock);
		pr_info("mi_disp_compat: status %08x %08x %08x %08x %08x %08x\n",
			((u32 *)tmp)[0], ((u32 *)tmp)[1], ((u32 *)tmp)[2],
			((u32 *)tmp)[3], ((u32 *)tmp)[4], ((u32 *)tmp)[5]);
		/* MI-SF FodMonitorThread::writeFPStatusToKernel: +8 u32 = 12 (field id), +12 u32 = fingerPrintStatus
		 * (1 = fingerprint session (enroll/auth) started, 2 = stopped) */
		if (((u32 *)tmp)[2] == 12) {
			u32 st = ((u32 *)tmp)[3];

			if (st == 1) {
				mi_fod_session = true;
			} else if (st == 2) {
				mi_fod_session = false;
			}
		}
		return 0;
	}
	pr_info("mi_disp_compat: unknown ioctl cmd=0x%x dir=%u size=%u nr=0x%x\n", cmd, _IOC_DIR(cmd), _IOC_SIZE(cmd), _IOC_NR(cmd));
	return -ENOTTY;
}

static const struct file_operations mi_disp_fops = {
	.owner = THIS_MODULE,
	.read = mi_disp_read,
	.write = mi_disp_write,
	.poll = mi_disp_poll,
	.unlocked_ioctl = mi_disp_ioctl,
	.compat_ioctl = mi_disp_ioctl,
	.llseek = no_llseek,
};

static struct miscdevice mi_disp_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mi_display/disp_feature",
	.fops = &mi_disp_fops,
	.mode = 0660,
};

static struct workqueue_struct *mi_fod_wq;

static int __init mi_disp_compat_init(void)
{
	INIT_KFIFO(mi_evq);
	mi_fod_wq = alloc_ordered_workqueue("mi_fod", 0);
	return misc_register(&mi_disp_misc);
}

/* ---- Xiaomi disp_param text protocol -------------------------------------------------------------------------
 * MI-SF  : setFODFlag(true/false) -> "0x80000" / "0x70000" (a fingerprint session needs the FOD light),
 *          setFingerprintState(v) -> "0xe000|v"
 * Goodix : gf_hal_set_hbm_cyan_status(1/0) -> "0x20000" / "0xf0000"
 * The FOD session flag gates what the touch controller's finger-in-FOD events do: panel HBM + the event that MI-SF turns
 * into IXiaomiFingerprint.extCmd(10, 6) ("FOD UI ready" for the Goodix HAL).
 */
static bool mi_fod_pressed_state;
static void mi_queue_event(u32 type, u32 value)
{
	u32 e[4] = { 0, type, MI_DISP_EVT_SZ, value };

	mutex_lock(&mi_evq_lock);
	kfifo_in(&mi_evq, (u8 *)e, sizeof(e));
	mutex_unlock(&mi_evq_lock);
	wake_up_interruptible(&mi_evq_wait);
}

/* Native chain (same idea as LineageOS): finger in FOD area (touch controller) -> sde_plane_is_fod_layer() flags the light
 * plane -> SDE FOD dim layer + panel HBM -> dsi_display_set_fod_ui(true) -> this event for MI-SF (extCmd(10,6) = FOD UI ready). */
static atomic_t mi_finger_down = ATOMIC_INIT(0);

struct mi_fod_work {
	struct delayed_work work;
	bool on;
};

static void mi_fod_work_fn(struct work_struct *w)
{
	struct mi_fod_work *fw = container_of(to_delayed_work(w), struct mi_fod_work, work);

	mi_queue_event(2, fw->on ? 0x7 : 0);
	mi_fod_pressed_state = fw->on;
	kfree(fw);
}

/* called by dsi_display_set_fod_ui() when the FOD dim layer comes or goes */
void mi_disp_compat_fod_ui_changed(bool on)
{
	struct mi_fod_work *fw;

	if (!mi_fod_wq)
		return;
	fw = kzalloc(sizeof(*fw), GFP_ATOMIC);
	if (!fw)
		return;
	fw->on = on;
	INIT_DELAYED_WORK(&fw->work, mi_fod_work_fn);
	/* give the panel ~100 ms to reach HBM before the HAL is told the UI is ready */
	queue_delayed_work(mi_fod_wq, &fw->work, on ? msecs_to_jiffies(100) : 0);
}
EXPORT_SYMBOL(mi_disp_compat_fod_ui_changed);

void mi_disp_compat_fod_pressed(bool pressed)
{
	atomic_set(&mi_finger_down, pressed);
}
EXPORT_SYMBOL(mi_disp_compat_fod_pressed);

bool mi_disp_compat_fod_finger_down(void)
{
	return atomic_read(&mi_finger_down);
}
EXPORT_SYMBOL(mi_disp_compat_fod_finger_down);

static ssize_t disp_param_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "fod_session=%d pressed=%d\n", mi_fod_session, mi_fod_pressed_state);
}

static ssize_t disp_param_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long v;

	if (kstrtoul(buf, 16, &v))
		return -EINVAL;
	switch (v) {
	case 0x80000:
		mi_fod_session = true;
		break;
	case 0x70000:
		mi_fod_session = false;
		break;
	case 0x20000:
		dsi_display_set_fod_hbm(true);
		break;
	case 0xF0000:
		dsi_display_set_fod_hbm(false);
		break;
	case 0xF1000: /* test: pretend a finger is in the FOD area (display path only) */
		mi_disp_compat_fod_pressed(true);
		break;
	case 0xF0100: /* test: finger released */
		mi_disp_compat_fod_pressed(false);
		break;
	default:
		pr_debug("mi_disp_compat: disp_param 0x%lx ignored\n", v);
		break;
	}
	return count;
}

static DEVICE_ATTR(disp_param, 0660, disp_param_show, disp_param_store);

int mi_disp_compat_attach_connector(struct device *kdev)
{
	return kdev ? device_create_file(kdev, &dev_attr_disp_param) : -ENODEV;
}
EXPORT_SYMBOL(mi_disp_compat_attach_connector);

void mi_disp_compat_detach_connector(struct device *kdev)
{
	if (kdev)
		device_remove_file(kdev, &dev_attr_disp_param);
}
EXPORT_SYMBOL(mi_disp_compat_detach_connector);

bool mi_disp_compat_fod_dump(void)
{
	return mi_fod_session || atomic_read(&mi_finger_down) || mi_fod_pressed_state;
}
EXPORT_SYMBOL(mi_disp_compat_fod_dump);

device_initcall(mi_disp_compat_init);
