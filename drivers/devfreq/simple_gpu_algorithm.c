/*
 * Author: Paul Reioux aka Faux123 <reioux@gmail.com>
 *
 * Copyright (c) 2011-2014 Paul Reioux <reioux@gmail.com>
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

#include <linux/module.h>
#include <linux/devfreq.h>
#include <linux/msm_adreno_devfreq.h>
#include <linux/io.h>

static int default_laziness = 2;
module_param_named(simple_laziness, default_laziness, int, 0664);

static int ramp_up_threshold = 6000;
module_param_named(simple_ramp_threshold, ramp_up_threshold, int, 0664);

int simple_gpu_active = 1;
module_param_named(simple_gpu_activate, simple_gpu_active, int, 0664);

static int laziness;

int simple_gpu_algorithm(int level, int *val,
			struct devfreq_msm_adreno_tz_data *priv)
{
	int ret = 0;

	/* sync memory before processing */
	__iowmb();

	/* ramp-up condition: GPU busy time exceeds threshold */
	if ((unsigned int)priv->bin.busy_time > ramp_up_threshold) {
		if (level == 0)
			ret = 0; /* already at highest frequency */
		else if (level > 0)
			ret = -1; /* ramp up to higher frequency level */
	/* ramp-down / idle condition */
	} else {
		if (level >= 0) {
			if (laziness > 0) {
				/* hold frequency for a while to avoid frame jitter */
				laziness--;
				ret = 0;
			} else {
				/* step down to lower frequency level */
				ret = 1;
				laziness = default_laziness;
			}
		}
	}
	*val = ret;

	return ret;
}
EXPORT_SYMBOL(simple_gpu_algorithm);

static int __init simple_gpu_init(void)
{
	laziness = default_laziness;
	return 0;
}
subsys_initcall(simple_gpu_init);

static void __exit simple_gpu_exit(void)
{
}
module_exit(simple_gpu_exit);

MODULE_AUTHOR("Paul Reioux <reioux@gmail.com>");
MODULE_DESCRIPTION("Simple GPU Control Algorithm for Adreno GPU");
MODULE_LICENSE("GPL");
