/* Copyright (c) 2017, The Linux Foundation. All rights reserved.
 * Copyright (c) 2018, Pal Zoltan Illes (tbalden) - kcal rgb
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/moduleparam.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <drm/drmP.h>
#include <drm/msm_drm_pp.h>
#include "sde_hw_color_proc_common_v4.h"
#include "sde_hw_color_proc_v4.h"

extern struct drm_device *kcal_drm_dev;

static unsigned short kcal_enable = 1;
static unsigned short kcal_min = 20;
static unsigned short kcal_red = 256;
static unsigned short kcal_green = 256;
static unsigned short kcal_blue = 256;
static unsigned short kcal_hue = 0;
static unsigned short kcal_sat = 255;
static unsigned short kcal_val = 255;
static unsigned short kcal_cont = 255;
static unsigned short kcal_invert = 0;

#define MAX_KCAL_CTX 4
static struct sde_hw_dspp *kcal_ctx_arr[MAX_KCAL_CTX];
static struct drm_msm_pcc kcal_pcc_payload[MAX_KCAL_CTX];
static bool kcal_pcc_valid[MAX_KCAL_CTX] = {false};
static int kcal_ctx_count = 0;

static void sde_kcal_update(struct sde_hw_dspp *ctx, int idx)
{
	struct drm_msm_pcc *pcc_cfg;
	struct drm_msm_pcc_coeff *coeffs = NULL;
	int i = 0;
	u32 base = 0;
	u32 opcode = 0, local_opcode = 0;

	if (!ctx || !kcal_pcc_valid[idx])
		return;

	pcc_cfg = &kcal_pcc_payload[idx];

	for (i = 0; i < PCC_NUM_PLANES; i++) {
		base = ctx->cap->sblk->pcc.base + (i * sizeof(u32));
		switch (i) {
		case 0: coeffs = &pcc_cfg->r; break;
		case 1: coeffs = &pcc_cfg->g; break;
		case 2: coeffs = &pcc_cfg->b; break;
		default: return;
		}

		SDE_REG_WRITE(&ctx->hw, base + PCC_R_OFF,
			(i == 0 && kcal_enable) ? (coeffs->r * kcal_red) / 256 : coeffs->r);
		SDE_REG_WRITE(&ctx->hw, base + PCC_G_OFF,
			(i == 1 && kcal_enable) ? (coeffs->g * kcal_green) / 256 : coeffs->g);
		SDE_REG_WRITE(&ctx->hw, base + PCC_B_OFF,
			(i == 2 && kcal_enable) ? (coeffs->b * kcal_blue) / 256 : coeffs->b);
	}

	opcode = SDE_REG_READ(&ctx->hw, ctx->cap->sblk->hsic.base);
	if (kcal_enable) {
		SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->hsic.base + PA_HUE_OFF, kcal_hue & PA_HUE_MASK);
		SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->hsic.base + PA_SAT_OFF, kcal_sat & PA_SAT_MASK);
		SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->hsic.base + PA_VAL_OFF, kcal_val & PA_VAL_MASK);
		SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->hsic.base + PA_CONT_OFF, kcal_cont & PA_CONT_MASK);
		local_opcode = PA_HUE_EN | PA_SAT_EN | PA_VAL_EN | PA_CONT_EN;
		opcode |= (local_opcode | PA_EN);
	}
	SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->hsic.base, opcode);
	SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->pcc.base, PCC_EN);
}

static void sde_kcal_apply(void)
{
	int i;

	if (kcal_drm_dev && kcal_drm_dev->dev) {
		if (pm_runtime_get_if_in_use(kcal_drm_dev->dev) > 0) {
			for (i = 0; i < kcal_ctx_count; i++) {
				sde_kcal_update(kcal_ctx_arr[i], i);
			}
			pm_runtime_put(kcal_drm_dev->dev);
		}
	}
}

static int kcal_param_set(const char *val, const struct kernel_param *kp)
{
	int ret = param_set_short(val, kp);

	if (ret)
		return ret;

	if (kcal_red < kcal_min)
		kcal_red = kcal_min;
	if (kcal_green < kcal_min)
		kcal_green = kcal_min;
	if (kcal_blue < kcal_min)
		kcal_blue = kcal_min;

	sde_kcal_apply();
	return 0;
}

static const struct kernel_param_ops kcal_param_ops = {
	.set = kcal_param_set,
	.get = param_get_short,
};

module_param_cb(kcal_red, &kcal_param_ops, &kcal_red, 0644);
module_param_cb(kcal_green, &kcal_param_ops, &kcal_green, 0644);
module_param_cb(kcal_blue, &kcal_param_ops, &kcal_blue, 0644);
module_param_cb(kcal_hue, &kcal_param_ops, &kcal_hue, 0644);
module_param_cb(kcal_sat, &kcal_param_ops, &kcal_sat, 0644);
module_param_cb(kcal_val, &kcal_param_ops, &kcal_val, 0644);
module_param_cb(kcal_cont, &kcal_param_ops, &kcal_cont, 0644);

/* Platform device sysfs attributes for EXKM / Savoca KCAL compatibility */
static ssize_t kcal_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%hu %hu %hu\n", kcal_red, kcal_green, kcal_blue);
}

static ssize_t kcal_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned short r, g, b;

	if (sscanf(buf, "%hu %hu %hu", &r, &g, &b) != 3)
		return -EINVAL;

	if (r > 256 || g > 256 || b > 256)
		return -EINVAL;

	kcal_red = max(r, kcal_min);
	kcal_green = max(g, kcal_min);
	kcal_blue = max(b, kcal_min);

	sde_kcal_apply();
	return count;
}
static DEVICE_ATTR_RW(kcal);

static ssize_t kcal_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%hu\n", kcal_enable);
}

static ssize_t kcal_enable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned short val;

	if (kstrtou16(buf, 10, &val))
		return -EINVAL;

	kcal_enable = val ? 1 : 0;
	sde_kcal_apply();
	return count;
}
static DEVICE_ATTR_RW(kcal_enable);

static ssize_t kcal_min_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%hu\n", kcal_min);
}

static ssize_t kcal_min_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned short val;

	if (kstrtou16(buf, 10, &val) || val < 1 || val > 256)
		return -EINVAL;

	kcal_min = val;
	if (kcal_red < kcal_min)
		kcal_red = kcal_min;
	if (kcal_green < kcal_min)
		kcal_green = kcal_min;
	if (kcal_blue < kcal_min)
		kcal_blue = kcal_min;

	sde_kcal_apply();
	return count;
}
static DEVICE_ATTR_RW(kcal_min);

static ssize_t kcal_sat_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%hu\n", kcal_sat);
}

static ssize_t kcal_sat_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned short val;

	if (kstrtou16(buf, 10, &val))
		return -EINVAL;

	kcal_sat = val;
	sde_kcal_apply();
	return count;
}
static DEVICE_ATTR_RW(kcal_sat);

static ssize_t kcal_val_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%hu\n", kcal_val);
}

static ssize_t kcal_val_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned short val;

	if (kstrtou16(buf, 10, &val))
		return -EINVAL;

	kcal_val = val;
	sde_kcal_apply();
	return count;
}
static DEVICE_ATTR_RW(kcal_val);

static ssize_t kcal_cont_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%hu\n", kcal_cont);
}

static ssize_t kcal_cont_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned short val;

	if (kstrtou16(buf, 10, &val))
		return -EINVAL;

	kcal_cont = val;
	sde_kcal_apply();
	return count;
}
static DEVICE_ATTR_RW(kcal_cont);

static ssize_t kcal_hue_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%hu\n", kcal_hue);
}

static ssize_t kcal_hue_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned short val;

	if (kstrtou16(buf, 10, &val))
		return -EINVAL;

	kcal_hue = val;
	sde_kcal_apply();
	return count;
}
static DEVICE_ATTR_RW(kcal_hue);

static ssize_t kcal_invert_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%hu\n", kcal_invert);
}

static ssize_t kcal_invert_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned short val;

	if (kstrtou16(buf, 10, &val))
		return -EINVAL;

	kcal_invert = val ? 1 : 0;
	return count;
}
static DEVICE_ATTR_RW(kcal_invert);

static struct attribute *kcal_attrs[] = {
	&dev_attr_kcal.attr,
	&dev_attr_kcal_enable.attr,
	&dev_attr_kcal_min.attr,
	&dev_attr_kcal_sat.attr,
	&dev_attr_kcal_val.attr,
	&dev_attr_kcal_cont.attr,
	&dev_attr_kcal_hue.attr,
	&dev_attr_kcal_invert.attr,
	NULL,
};

static const struct attribute_group kcal_attr_group = {
	.attrs = kcal_attrs,
};

static struct platform_device kcal_ctrl_device = {
	.name = "kcal_ctrl",
	.id = 0,
};

static int __init kcal_platform_init(void)
{
	int ret;

	ret = platform_device_register(&kcal_ctrl_device);
	if (ret) {
		pr_err("kcal: failed to register platform device: %d\n", ret);
		return ret;
	}

	ret = sysfs_create_group(&kcal_ctrl_device.dev.kobj, &kcal_attr_group);
	if (ret) {
		pr_err("kcal: failed to create sysfs group: %d\n", ret);
		platform_device_unregister(&kcal_ctrl_device);
		return ret;
	}

	pr_info("kcal: initialized /sys/devices/platform/kcal_ctrl.0\n");
	return 0;
}
late_initcall(kcal_platform_init);

static int sde_write_3d_gamut(struct sde_hw_blk_reg_map *hw,
		struct drm_msm_3d_gamut *payload, u32 base,
		u32 *opcode)
{
	u32 reg, tbl_len, tbl_off, scale_off, i, j;
	u32 scale_tbl_len, scale_tbl_off;
	u32 *scale_data;

	if (!payload || !opcode || !hw) {
		DRM_ERROR("invalid payload %pK opcode %pK hw %pK\n",
			payload, opcode, hw);
		return -EINVAL;
	}

	switch (payload->mode) {
	case GAMUT_3D_MODE_17:
		tbl_len = GAMUT_3D_MODE17_TBL_SZ;
		tbl_off = 0;
		scale_off = GAMUT_SCALEA_OFFSET_OFF;
		*opcode = gamut_mode_17 << 2;
		break;
	case GAMUT_3D_MODE_13:
		*opcode = (*opcode & (BIT(4) - 1)) >> 2;
		if (*opcode == gamut_mode_13a)
			*opcode = gamut_mode_13b;
		else
			*opcode = gamut_mode_13a;
		tbl_len = GAMUT_3D_MODE13_TBL_SZ;
		tbl_off = (*opcode == gamut_mode_13a) ? 0 :
			GAMUT_MODE_13B_OFF;
		scale_off = (*opcode == gamut_mode_13a) ?
			GAMUT_SCALEA_OFFSET_OFF : GAMUT_SCALEB_OFFSET_OFF;
		*opcode <<= 2;
		break;
	case GAMUT_3D_MODE_5:
		*opcode = gamut_mode_5 << 2;
		tbl_len = GAMUT_3D_MODE5_TBL_SZ;
		tbl_off = GAMUT_MODE_5_OFF;
		scale_off = GAMUT_SCALEB_OFFSET_OFF;
		break;
	default:
		DRM_ERROR("invalid mode %d\n", payload->mode);
		return -EINVAL;
	}

	if (payload->flags & GAMUT_3D_MAP_EN)
		*opcode |= GAMUT_MAP_EN;
	*opcode |= GAMUT_EN;

	for (i = 0; i < GAMUT_3D_TBL_NUM; i++) {
		reg = GAMUT_TABLE0_SEL << i;
		reg |= ((tbl_off) & (BIT(11) - 1));
		SDE_REG_WRITE(hw, base + GAMUT_TABLE_SEL_OFF, reg);
		for (j = 0; j < tbl_len; j++) {
			SDE_REG_WRITE(hw, base + GAMUT_LOWER_COLOR_OFF,
					payload->col[i][j].c2_c1);
			SDE_REG_WRITE(hw, base + GAMUT_UPPER_COLOR_OFF,
					payload->col[i][j].c0);
		}
	}

	if ((*opcode & GAMUT_MAP_EN)) {
		if (scale_off == GAMUT_SCALEA_OFFSET_OFF)
			scale_tbl_len = GAMUT_3D_SCALE_OFF_SZ;
		else
			scale_tbl_len = GAMUT_3D_SCALEB_OFF_SZ;
		for (i = 0; i < GAMUT_3D_SCALE_OFF_TBL_NUM; i++) {
			scale_tbl_off = base + scale_off +
					i * scale_tbl_len * sizeof(u32);
			scale_data = &payload->scale_off[i][0];
			for (j = 0; j < scale_tbl_len; j++)
				SDE_REG_WRITE(hw,
					scale_tbl_off + (j * sizeof(u32)),
					scale_data[j]);
		}
	}
	SDE_REG_WRITE(hw, base, *opcode);
	return 0;
}

void sde_setup_dspp_3d_gamutv4(struct sde_hw_dspp *ctx, void *cfg)
{
	struct drm_msm_3d_gamut *payload;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	u32 op_mode;

	if (!ctx || !cfg) {
		DRM_ERROR("invalid param ctx %pK cfg %pK\n", ctx, cfg);
		return;
	}

	op_mode = SDE_REG_READ(&ctx->hw, ctx->cap->sblk->gamut.base);
	if (!hw_cfg->payload) {
		DRM_DEBUG_DRIVER("disable gamut feature\n");
		SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->gamut.base, 0);
		return;
	}

	payload = hw_cfg->payload;
	sde_write_3d_gamut(&ctx->hw, payload, ctx->cap->sblk->gamut.base,
		&op_mode);

}

void sde_setup_dspp_igcv3(struct sde_hw_dspp *ctx, void *cfg)
{
	struct drm_msm_igc_lut *lut_cfg;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	int i = 0, j = 0;
	u32 *addr = NULL;
	u32 offset = 0;

	if (!ctx || !cfg) {
		DRM_ERROR("invalid param ctx %pK cfg %pK\n", ctx, cfg);
		return;
	}

	if (!hw_cfg->payload) {
		DRM_DEBUG_DRIVER("disable igc feature\n");
		SDE_REG_WRITE(&ctx->hw, IGC_OPMODE_OFF, 0);
		return;
	}

	if (hw_cfg->len != sizeof(struct drm_msm_igc_lut)) {
		DRM_ERROR("invalid size of payload len %d exp %zd\n",
				hw_cfg->len, sizeof(struct drm_msm_igc_lut));
		return;
	}

	lut_cfg = hw_cfg->payload;

	for (i = 0; i < IGC_TBL_NUM; i++) {
		addr = lut_cfg->c0 + (i * ARRAY_SIZE(lut_cfg->c0));
		offset = IGC_C0_OFF + (i * sizeof(u32));

		for (j = 0; j < IGC_TBL_LEN; j++) {
			addr[j] &= IGC_DATA_MASK;
			addr[j] |= IGC_DSPP_SEL_MASK(ctx->idx - 1);
			if (j == 0)
				addr[j] |= IGC_INDEX_UPDATE;
			/* IGC lut registers are part of DSPP Top HW block */
			SDE_REG_WRITE(&ctx->hw_top, offset, addr[j]);
		}
	}

	if (lut_cfg->flags & IGC_DITHER_ENABLE) {
		SDE_REG_WRITE(&ctx->hw, IGC_DITHER_OFF,
			lut_cfg->strength & IGC_DITHER_DATA_MASK);
	}

	SDE_REG_WRITE(&ctx->hw, IGC_OPMODE_OFF, IGC_EN);
}

void sde_setup_dspp_pccv4(struct sde_hw_dspp *ctx, void *cfg)
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct drm_msm_pcc *pcc_cfg;
	struct drm_msm_pcc_coeff *coeffs = NULL;
	bool found = false;
	int j_idx = 0;
	int i = 0;
	u32 base = 0;
	u32 opcode = 0, local_opcode = 0;

	if (ctx && cfg) {
		for (j_idx = 0; j_idx < kcal_ctx_count; j_idx++) {
			if (kcal_ctx_arr[j_idx] == ctx) {
				found = true;
				break;
			}
		}
		if (!found && kcal_ctx_count < MAX_KCAL_CTX) {
			j_idx = kcal_ctx_count;
			kcal_ctx_arr[j_idx] = ctx;
			kcal_ctx_count++;
		}

		if (j_idx < MAX_KCAL_CTX && hw_cfg->payload &&
		    hw_cfg->len == sizeof(struct drm_msm_pcc)) {
			memcpy(&kcal_pcc_payload[j_idx], hw_cfg->payload,
			       sizeof(struct drm_msm_pcc));
			kcal_pcc_valid[j_idx] = true;
		}
	}

	if (!ctx || !cfg) {
		DRM_ERROR("invalid param ctx %pK cfg %pK\n", ctx, cfg);
		return;
	}

	if (kcal_red < kcal_min)
		kcal_red = kcal_min;
	if (kcal_green < kcal_min)
		kcal_green = kcal_min;
	if (kcal_blue < kcal_min)
		kcal_blue = kcal_min;

	if (!hw_cfg->payload) {
		DRM_DEBUG_DRIVER("disable pcc feature\n");
		SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->pcc.base, 0);
		return;
	}

	if (hw_cfg->len != sizeof(struct drm_msm_pcc)) {
		DRM_ERROR("invalid size of payload len %d exp %zd\n",
				hw_cfg->len, sizeof(struct drm_msm_pcc));
		return;
	}

	pcc_cfg = hw_cfg->payload;

	for (i = 0; i < PCC_NUM_PLANES; i++) {
		base = ctx->cap->sblk->pcc.base + (i * sizeof(u32));
		switch (i) {
		case 0:
			coeffs = &pcc_cfg->r;
			SDE_REG_WRITE(&ctx->hw,
				base + PCC_RR_OFF, pcc_cfg->r_rr);
			SDE_REG_WRITE(&ctx->hw,
				base + PCC_GG_OFF, pcc_cfg->r_gg);
			SDE_REG_WRITE(&ctx->hw,
				base + PCC_BB_OFF, pcc_cfg->r_bb);
			break;
		case 1:
			coeffs = &pcc_cfg->g;
			SDE_REG_WRITE(&ctx->hw,
				base + PCC_RR_OFF, pcc_cfg->g_rr);
			SDE_REG_WRITE(&ctx->hw,
				base + PCC_GG_OFF, pcc_cfg->g_gg);
			SDE_REG_WRITE(&ctx->hw,
				base + PCC_BB_OFF, pcc_cfg->g_bb);
			break;
		case 2:
			coeffs = &pcc_cfg->b;
			SDE_REG_WRITE(&ctx->hw,
				base + PCC_RR_OFF, pcc_cfg->b_rr);
			SDE_REG_WRITE(&ctx->hw,
				base + PCC_GG_OFF, pcc_cfg->b_gg);
			SDE_REG_WRITE(&ctx->hw,
				base + PCC_BB_OFF, pcc_cfg->b_bb);
			break;
		default:
			DRM_ERROR("invalid pcc plane: %d\n", i);
			return;
		}

		SDE_REG_WRITE(&ctx->hw, base + PCC_C_OFF, coeffs->c);

		// RED
		SDE_REG_WRITE(&ctx->hw, base + PCC_R_OFF,
			(i == 0 && kcal_enable) ? (coeffs->r * kcal_red) / 256 : coeffs->r);
		// GREEN
		SDE_REG_WRITE(&ctx->hw, base + PCC_G_OFF,
			(i == 1 && kcal_enable) ? (coeffs->g * kcal_green) / 256 : coeffs->g);
		// BLUE
		SDE_REG_WRITE(&ctx->hw, base + PCC_B_OFF,
			(i == 2 && kcal_enable) ? (coeffs->b * kcal_blue) / 256 : coeffs->b);

		SDE_REG_WRITE(&ctx->hw, base + PCC_RG_OFF, coeffs->rg);
		SDE_REG_WRITE(&ctx->hw, base + PCC_RB_OFF, coeffs->rb);
		SDE_REG_WRITE(&ctx->hw, base + PCC_GB_OFF, coeffs->gb);
		SDE_REG_WRITE(&ctx->hw, base + PCC_RGB_OFF, coeffs->rgb);
	}

	if (kcal_enable) {
		opcode = SDE_REG_READ(&ctx->hw, ctx->cap->sblk->hsic.base);

		// HUE
		SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->hsic.base + PA_HUE_OFF,
			kcal_hue & PA_HUE_MASK);
		local_opcode |= PA_HUE_EN;

		// SATURATION
		SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->hsic.base + PA_SAT_OFF,
			kcal_sat & PA_SAT_MASK);
		local_opcode |= PA_SAT_EN;

		// VALUE
		SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->hsic.base + PA_VAL_OFF,
			kcal_val & PA_VAL_MASK);
		local_opcode |= PA_VAL_EN;

		// CONTRAST
		SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->hsic.base + PA_CONT_OFF,
			kcal_cont & PA_CONT_MASK);
		local_opcode |= PA_CONT_EN;

		opcode |= (local_opcode | PA_EN);
		SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->hsic.base, opcode);
	}

	SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->pcc.base, PCC_EN);
}
