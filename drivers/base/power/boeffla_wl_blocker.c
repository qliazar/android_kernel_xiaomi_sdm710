/*
 * Author: andip71, 01.09.2017
 *
 * Version 1.1.0
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/*
 * Change log:
 *
 * 1.1.0 (01.09.2017)
 *   - By default, selected wakelocks are blocked in an own list
 *
 * 1.0.1 (29.08.2017)
 *   - Add killing wakelock when currently active
 *
 * 1.0.0 (28.08.2017)
 *   - Initial version
 *
 */

#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include "boeffla_wl_blocker.h"

/*****************************************/
// Variables
/*****************************************/

char list_wl[LENGTH_LIST_WL] = {0};
char list_wl_default[LENGTH_LIST_WL_DEFAULT] = {0};

char list_wl_search[LENGTH_LIST_WL_SEARCH] = {0};
bool wl_blocker_active = false;
bool wl_blocker_debug = false;

/*****************************************/
// internal functions
/*****************************************/

void build_search_string(const char *list1, const char *list2)
{
	char *p;

	// store wakelock list and search string (with semicolons added at start and end)
	snprintf(list_wl_search, sizeof(list_wl_search), ";%s;%s;",
		 list1 ? list1 : "", list2 ? list2 : "");

	// normalize delimiters (spaces, commas, newlines) to semicolons
	for (p = list_wl_search; *p; p++) {
		if (*p == ',' || *p == ' ' || *p == '\n' || *p == '\r')
			*p = ';';
	}

	// set flag if wakelock blocker should be active (for performance reasons)
	if (strlen(list_wl_search) > 5)
		wl_blocker_active = true;
	else
		wl_blocker_active = false;
}

/*****************************************/
// sysfs interface functions
/*****************************************/

// show list of user configured wakelocks
static ssize_t wakelock_blocker_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s\n", list_wl);
}

// store list of user configured wakelocks
static ssize_t wakelock_blocker_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t n)
{
	char *nl;

	if (n >= LENGTH_LIST_WL)
		return -EINVAL;

	strlcpy(list_wl, buf, sizeof(list_wl));

	nl = strchr(list_wl, '\n');
	if (nl)
		*nl = '\0';
	nl = strchr(list_wl, '\r');
	if (nl)
		*nl = '\0';

	build_search_string(list_wl_default, list_wl);

	return n;
}

// show list of default, predefined wakelocks
static ssize_t wakelock_blocker_default_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s\n", list_wl_default);
}

// store list of default, predefined wakelocks
static ssize_t wakelock_blocker_default_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t n)
{
	char *nl;

	if (n >= LENGTH_LIST_WL_DEFAULT)
		return -EINVAL;

	strlcpy(list_wl_default, buf, sizeof(list_wl_default));

	nl = strchr(list_wl_default, '\n');
	if (nl)
		*nl = '\0';
	nl = strchr(list_wl_default, '\r');
	if (nl)
		*nl = '\0';

	build_search_string(list_wl_default, list_wl);

	return n;
}

// show debug information of driver internals
static ssize_t debug_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "Debug status: %d\n\nUser list: %s\nDefault list: %s\nSearch list: %s\nActive: %d\n",
					wl_blocker_debug, list_wl, list_wl_default, list_wl_search, wl_blocker_active);
}

// store debug mode on/off (1/0)
static ssize_t debug_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t count)
{
	unsigned int val;

	if (sscanf(buf, "%u", &val) != 1)
		return -EINVAL;

	if (val == 1)
		wl_blocker_debug = true;
	else
		wl_blocker_debug = false;

	return count;
}

static ssize_t version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s\n", BOEFFLA_WL_BLOCKER_VERSION);
}

/*****************************************/
// Initialize sysfs objects
/*****************************************/

static DEVICE_ATTR(wakelock_blocker, 0644, wakelock_blocker_show, wakelock_blocker_store);
static DEVICE_ATTR(wakelock_blocker_default, 0644, wakelock_blocker_default_show, wakelock_blocker_default_store);
static DEVICE_ATTR(debug, 0664, debug_show, debug_store);
static DEVICE_ATTR(version, 0444, version_show, NULL);

static struct attribute *boeffla_wl_blocker_attributes[] = {
	&dev_attr_wakelock_blocker.attr,
	&dev_attr_wakelock_blocker_default.attr,
	&dev_attr_debug.attr,
	&dev_attr_version.attr,
	NULL
};

static struct attribute_group boeffla_wl_blocker_control_group = {
	.attrs = boeffla_wl_blocker_attributes,
};

static struct miscdevice boeffla_wl_blocker_control_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "boeffla_wakelock_blocker",
};

/*****************************************/
// Driver init and exit functions
/*****************************************/

static int __init boeffla_wl_blocker_init(void)
{
	int ret;

	ret = misc_register(&boeffla_wl_blocker_control_device);
	if (ret) {
		pr_err("Boeffla WL blocker: misc_register failed: %d\n", ret);
		return ret;
	}

	ret = sysfs_create_group(&boeffla_wl_blocker_control_device.this_device->kobj,
				&boeffla_wl_blocker_control_group);
	if (ret) {
		pr_err("Boeffla WL blocker: failed to create sysfs object\n");
		misc_deregister(&boeffla_wl_blocker_control_device);
		return ret;
	}

	// initialize default list
	snprintf(list_wl_default, sizeof(list_wl_default), "%s", LIST_WL_DEFAULT);
	build_search_string(list_wl_default, list_wl);

	pr_info("Boeffla WL blocker: driver version %s started\n", BOEFFLA_WL_BLOCKER_VERSION);

	return 0;
}

static void __exit boeffla_wl_blocker_exit(void)
{
	sysfs_remove_group(&boeffla_wl_blocker_control_device.this_device->kobj,
                           &boeffla_wl_blocker_control_group);
	misc_deregister(&boeffla_wl_blocker_control_device);

	pr_info("Boeffla WL blocker: driver stopped\n");
}

module_init(boeffla_wl_blocker_init);
module_exit(boeffla_wl_blocker_exit);
