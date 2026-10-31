/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MI_DISP_COMPAT_H
#define _LINUX_MI_DISP_COMPAT_H

#include <linux/types.h>

struct device;


/* connector sysfs attribute "disp_param" (Xiaomi text protocol used by MI-SF and the Goodix HAL wrapper) */
int mi_disp_compat_attach_connector(struct device *kdev);
void mi_disp_compat_detach_connector(struct device *kdev);

/* touch controller reports finger down/up inside the FOD area */
void mi_disp_compat_fod_pressed(bool pressed);
bool mi_disp_compat_fod_finger_down(void);
void mi_disp_compat_fod_ui_changed(bool on);

/* true while a FOD session / press is active (used for DRM plane debug dumps) */
bool mi_disp_compat_fod_dump(void);

/* provided by dsi_display.c: panel HBM on/off */
int dsi_display_set_fod_hbm(bool on);

#endif
