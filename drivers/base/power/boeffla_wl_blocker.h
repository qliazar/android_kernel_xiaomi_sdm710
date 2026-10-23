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

#ifndef _BOEFFLA_WL_BLOCKER_H
#define _BOEFFLA_WL_BLOCKER_H

#include <linux/types.h>

#define BOEFFLA_WL_BLOCKER_VERSION	"1.1.0"

#define LIST_WL_DEFAULT			"qcom_rx_wakelock;wlan;wlan_wow_wl;wlan_extscan_wl;netmgr_wl;NETLINK;IPA_WS;wlan_ipa;wlan_pno_wl;wcnss_filter_lock"

#define LENGTH_LIST_WL			1024
#define LENGTH_LIST_WL_DEFAULT		512
#define LENGTH_LIST_WL_SEARCH		(LENGTH_LIST_WL + LENGTH_LIST_WL_DEFAULT + 8)

extern char list_wl[LENGTH_LIST_WL];
extern char list_wl_default[LENGTH_LIST_WL_DEFAULT];
extern char list_wl_search[LENGTH_LIST_WL_SEARCH];
extern bool wl_blocker_active;
extern bool wl_blocker_debug;
extern bool enable_wakelock_blocker;

void build_search_string(const char *list1, const char *list2);

#endif /* _BOEFFLA_WL_BLOCKER_H */
