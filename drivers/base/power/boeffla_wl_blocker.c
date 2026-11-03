// SPDX-License-Identifier: GPL-2.0
/*
 * Boeffla Wakelock Blocker
 * Copyright (C) 2014-2016 Andi Pantea (Lord Boeffla)
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/string.h>
#include <linux/mutex.h>

#define BOEFFLA_WL_BLOCKER_VERSION "1.1.0"
#define LENGTH_LIST_WL 2048

static char list_wl[LENGTH_LIST_WL];
static const char list_wl_default[] =
	"qcom_rx_wakelock;wlan;wlan_wow_wl;wlan_extscan_wl;netmgr_wl;NETLINK";
static char list_wl_search[LENGTH_LIST_WL * 2 + 8];
static bool wl_blocker_active;
static bool debug;
static DEFINE_MUTEX(wl_blocker_mutex);

static void build_search_string(void)
{
	snprintf(list_wl_search, sizeof(list_wl_search), ";%s;%s;", list_wl_default, list_wl);
	wl_blocker_active = (strlen(list_wl_search) > 5);
}

bool boeffla_is_wakelock_blocked(const char *name)
{
	char token[128];

	if (!wl_blocker_active || !name)
		return false;

	if (snprintf(token, sizeof(token), ";%s;", name) >= sizeof(token))
		return false;

	if (strstr(list_wl_search, token)) {
		if (unlikely(debug))
			pr_info("boeffla_wl_blocker: blocked '%s'\n", name);
		return true;
	}
	return false;
}
EXPORT_SYMBOL_GPL(boeffla_is_wakelock_blocked);

static ssize_t wakelock_blocker_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s\n", list_wl);
}

static ssize_t wakelock_blocker_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	if (count >= LENGTH_LIST_WL)
		return -EINVAL;

	mutex_lock(&wl_blocker_mutex);
	strlcpy(list_wl, buf, sizeof(list_wl));
	strim(list_wl);
	build_search_string();
	mutex_unlock(&wl_blocker_mutex);

	return count;
}

static ssize_t wakelock_blocker_default_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s\n", list_wl_default);
}

static ssize_t debug_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", debug);
}

static ssize_t debug_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	bool val;

	if (kstrtobool(buf, &val) < 0)
		return -EINVAL;

	debug = val;
	return count;
}

static ssize_t version_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s\n", BOEFFLA_WL_BLOCKER_VERSION);
}

static DEVICE_ATTR_RW(wakelock_blocker);
static DEVICE_ATTR_RO(wakelock_blocker_default);
static DEVICE_ATTR_RW(debug);
static DEVICE_ATTR_RO(version);

static struct attribute *boeffla_wl_blocker_attrs[] = {
	&dev_attr_wakelock_blocker.attr,
	&dev_attr_wakelock_blocker_default.attr,
	&dev_attr_debug.attr,
	&dev_attr_version.attr,
	NULL,
};
ATTRIBUTE_GROUPS(boeffla_wl_blocker);

static struct miscdevice boeffla_wl_blocker_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "boeffla_wakelock_blocker",
	.groups = boeffla_wl_blocker_groups,
};

static int __init boeffla_wl_blocker_init(void)
{
	build_search_string();
	return misc_register(&boeffla_wl_blocker_dev);
}
late_initcall(boeffla_wl_blocker_init);
