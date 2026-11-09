// SPDX-License-Identifier: GPL-2.0-only
/*
 * Minimal Sony IMX351 V4L2 subdev for the Surface Duo 1 (camera step 4, docs/research/33 "CAMERA").
 * One fixed mode: 4032x2268 RAW10 (Android's preview mode), 4 CSI-2 lanes, link 828 MHz (REQ_LINK_BIT_RATE 6624 Mbps), BGGR
 * (the init table sets IMAGE_ORIENTATION 0x0101 = 3). The register tables are NOT in this file: imx351-camx-regs.h is
 * generated locally from a Droidian CCI trace (tools/linux/camera/cci-trace-decode.py + cci-trace-header.py).
 * Power-up like the downstream sensor XML: vio -> power gpio (board vana/vdig switch) -> MCLK 19.2 MHz -> reset release.
 * Structure follows the mainline Sony drivers (imx355/imx219) with the v4l2-cci helpers; no exposure/gain controls yet.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#include "imx351-camx-regs.h"

#define IMX351_REG_MODEL_ID	CCI_REG16(0x0016)
#define IMX351_MODEL_ID		0x0351
#define IMX351_REG_MODE_SELECT	CCI_REG8(0x0100)
#define IMX351_FULL_W		4032
#define IMX351_FULL_H		2268

/*
 * mode=1: 1920x1080 at ~30 fps (2x2 binning of the 16:9 readout window + a centred digital crop), from the camx 1080p
 * table (vendor_a sensormodule, table 0x61168): only PLL_MPY, frame/line length, exposure, binning and the output /
 * digital-crop sizes differ from the full mode; the CSI link rate stays the same (docs/research/workbench/camera.md).
 */
static int mode;
module_param(mode, int, 0644);
MODULE_PARM_DESC(mode, "0 = 4032x2268 (default), 1 = 1920x1080 2x2 binned (~30 fps), 2 = 3712x2784 4:3, 3 = 2784x2784 1:1");
/*
 * mode=2 / mode=3 (2026-11-07): Android's 4:3 (3712x2784) and square (2784x2784) photo modes, i.e. the full 4656x3496
 * array read out (Y 0..3495) and a centred digital crop; FLL 4640 x LLP 6032 = 42.3 ms (~23.7 fps). Their register
 * deltas against the 16:9 table come from the phone's own camx module (camera/camx-mode-delta.py -> the LOCAL header,
 * arrays imx351_43_regs / imx351_sq_regs); PLL, lanes and link are identical, so link-freq / pixel-rate stay.
 */
/* mode=4: 4032x2784 (Android's wide photo mode, readout Y 356..3139); mode=5: 1920x1080 2x2 binned HIGH SPEED (camx
 * FLL 1152 lines = ~199 fps, slow motion; selectable only by the mode= parameter: same size as mode 1). ALL modes the
 * phone's camx table defines (request 2026-11-07: "build drivers for the whole default table"); since then set_fmt picks
 * the mode matching the requested size and enum_frame_size lists every available one. */
#define IMX351_WIDTH		(mode == 1 || mode == 5 ? 1920 : mode == 2 ? 3712 : mode == 3 ? 2784 : IMX351_FULL_W)
#define IMX351_HEIGHT		(mode == 1 || mode == 5 ? 1080 : mode >= 2 && mode <= 4 ? 2784 : IMX351_FULL_H)

static const struct cci_reg_sequence imx351_1080p_regs[] = {
	{ CCI_REG8(0x0307), 0x1c },			/* PLL_MPY (full: 0x48) */
	{ CCI_REG8(0x0340), 0x08 }, { CCI_REG8(0x0341), 0x2c },	/* FRM_LENGTH 2092 */
	{ CCI_REG8(0x0342), 0x0b }, { CCI_REG8(0x0343), 0x50 },	/* LINE_LENGTH 2896 */
	{ CCI_REG8(0x0202), 0x08 }, { CCI_REG8(0x0203), 0x18 },	/* coarse exposure */
	{ CCI_REG8(0x0900), 0x01 }, { CCI_REG8(0x0901), 0x22 },	/* 2x2 binning */
	{ CCI_REG8(0x0408), 0x00 }, { CCI_REG8(0x0409), 0x30 },	/* dig crop x offset 48 = (2016-1920)/2 */
	{ CCI_REG8(0x040a), 0x00 }, { CCI_REG8(0x040b), 0x1b },	/* dig crop y offset 27 = (1134-1080)/2 */
	{ CCI_REG8(0x040c), 0x07 }, { CCI_REG8(0x040d), 0x80 },	/* dig crop 1920 */
	{ CCI_REG8(0x040e), 0x04 }, { CCI_REG8(0x040f), 0x38 },	/* x 1080 */
	{ CCI_REG8(0x034c), 0x07 }, { CCI_REG8(0x034d), 0x80 },	/* X_OUT_SIZE 1920 */
	{ CCI_REG8(0x034e), 0x04 }, { CCI_REG8(0x034f), 0x38 },	/* Y_OUT_SIZE 1080 */
};
#define IMX351_LINK_FREQ	828000000ULL
#define IMX351_PIXEL_RATE	662400000LL	/* link * 2 * 4 lanes / 10 bpp */
#define IMX351_XCLK		19200000

static const s64 imx351_link_freqs[] = { IMX351_LINK_FREQ };

/* per-mode timing for the standard sensor controls (libcamera / AE): frame + line length (lines / pixel clocks),
 * defaults as the tables write them (full mode: the Droidian trace; 1080p: camx table 0x61168) */
/* Default frame length. Mode 1 used 2092 lines = 9.1 ms (~109 fps; line = LLP 2896 / 662.4 MHz = 4.37 us), which caps
 * the exposure (FLL - margin) at ~9 ms - and libcamera's soft AE never touches VBLANK, so indoor pictures were dark
 * and green (2026-11-06, workbench/camera.md). 7624 lines = 33.3 ms = 30 fps like Android (which runs ~5048 lines in
 * its 4032x2268 mode): up to 33 ms exposure. Mode 0 (5068 lines x 8.37 us = 42 ms) stays. */
#define IMX351_FLL		(mode == 1 ? 7624 : mode == 5 ? 1152 : mode >= 2 ? 4640 : 5068)
#define IMX351_LLP		(mode == 1 || mode == 5 ? 2896 : mode >= 2 ? 6032 : 5544)
#define IMX351_EXP_DEF		(mode == 1 ? 2072 : mode == 5 ? 1132 : mode >= 2 ? 4620 : 5048)
#define IMX351_EXP_MARGIN	20
#define IMX351_GAIN_MAX		978		/* code; gain = 1024 / (1024 - code) */
#define IMX351_GAIN_DEF		(mode == 1 ? 0 : 626)
#define IMX351_REG_EXPOSURE	CCI_REG16(0x0202)
#define IMX351_REG_AGAIN	CCI_REG16(0x0204)
#define IMX351_REG_FLL		CCI_REG16(0x0340)

struct imx351 {
	struct device *dev;
	struct regmap *regmap;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrls;
	struct v4l2_ctrl *exposure, *vblank, *hblank;
	bool streaming;
	struct clk *xclk;
	struct regulator *vio;
	struct regulator *vana;	/* optional: the module power switch as a shared regulator (also feeds the AF lens) */
	struct gpio_desc *power;
	struct gpio_desc *reset;
};

static inline struct imx351 *to_imx351(struct v4l2_subdev *sd) { return container_of(sd, struct imx351, sd); }

static int imx351_power_on(struct device *dev)
{
	struct imx351 *s = to_imx351(dev_get_drvdata(dev));
	int ret;

	ret = regulator_enable(s->vio);
	if (ret)
		return ret;
	if (s->vana) {
		ret = regulator_enable(s->vana);
		if (ret) {
			regulator_disable(s->vio);
			return ret;
		}
	}
	gpiod_set_value_cansleep(s->reset, 1);		/* asserted (active low in DT) */
	gpiod_set_value_cansleep(s->power, 1);
	usleep_range(5000, 6000);
	ret = clk_prepare_enable(s->xclk);
	if (ret) {
		gpiod_set_value_cansleep(s->power, 0);
		if (s->vana)
			regulator_disable(s->vana);
		regulator_disable(s->vio);
		return ret;
	}
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(s->reset, 0);
	usleep_range(10000, 11000);
	return 0;
}

static int imx351_power_off(struct device *dev)
{
	struct imx351 *s = to_imx351(dev_get_drvdata(dev));

	gpiod_set_value_cansleep(s->reset, 1);
	clk_disable_unprepare(s->xclk);
	gpiod_set_value_cansleep(s->power, 0);
	if (s->vana)
		regulator_disable(s->vana);
	regulator_disable(s->vio);
	return 0;
}

static void imx351_fill_fmt(struct v4l2_mbus_framefmt *f)
{
	f->width = IMX351_WIDTH;
	f->height = IMX351_HEIGHT;
	f->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	f->field = V4L2_FIELD_NONE;
	f->colorspace = V4L2_COLORSPACE_RAW;
	f->ycbcr_enc = V4L2_YCBCR_ENC_601;
	f->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	f->xfer_func = V4L2_XFER_FUNC_NONE;
}

static int imx351_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx351 *s = container_of(ctrl->handler, struct imx351, ctrls);
	int ret = 0;

	if (ctrl->id == V4L2_CID_VBLANK) {	/* keep the exposure inside the new frame length */
		int max = IMX351_HEIGHT + ctrl->val - IMX351_EXP_MARGIN;
		__v4l2_ctrl_modify_range(s->exposure, 1, max, 1, min_t(int, s->exposure->val, max));
	}
	if (!pm_runtime_get_if_in_use(s->dev))
		return 0;			/* applied at stream on (__v4l2_ctrl_handler_setup) */
	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		ret = cci_write(s->regmap, IMX351_REG_EXPOSURE, ctrl->val, NULL);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = cci_write(s->regmap, IMX351_REG_AGAIN, ctrl->val, NULL);
		break;
	case V4L2_CID_VBLANK:
		ret = cci_write(s->regmap, IMX351_REG_FLL, IMX351_HEIGHT + ctrl->val, NULL);
		break;
	}
	pm_runtime_put(s->dev);
	return ret;
}

static const struct v4l2_ctrl_ops imx351_ctrl_ops = { .s_ctrl = imx351_set_ctrl };

static int imx351_enable_streams(struct v4l2_subdev *sd, struct v4l2_subdev_state *state, u32 pad, u64 streams)
{
	struct imx351 *s = to_imx351(sd);
	int ret;

	ret = pm_runtime_resume_and_get(s->dev);
	if (ret)
		return ret;
	ret = cci_multi_reg_write(s->regmap, imx351_init_regs, ARRAY_SIZE(imx351_init_regs), NULL);
	if (!ret)
		ret = cci_multi_reg_write(s->regmap, imx351_mode_regs, ARRAY_SIZE(imx351_mode_regs), NULL);
	/*
	 * mode 1: the FULL camx 1080p delta when the local header has it (2026-11-07). The hand-picked imx351_1080p_regs
	 * subset missed registers of Android's 1080p table, and its readout window sat off-centre: on a flat field 25 % of
	 * the width x 76 % of the height came out black (outside the lens image circle) vs 3 % x 6 % in the 4:3 mode.
	 */
#ifdef IMX351_1080_REGS_PRESENT
	if (!ret && mode == 1)
		ret = cci_multi_reg_write(s->regmap, imx351_1080_regs, ARRAY_SIZE(imx351_1080_regs), NULL);
#else
	if (!ret && mode == 1)
		ret = cci_multi_reg_write(s->regmap, imx351_1080p_regs, ARRAY_SIZE(imx351_1080p_regs), NULL);
#endif
#ifdef IMX351_43_REGS_PRESENT
	if (!ret && mode == 2)
		ret = cci_multi_reg_write(s->regmap, imx351_43_regs, ARRAY_SIZE(imx351_43_regs), NULL);
#endif
#ifdef IMX351_SQ_REGS_PRESENT
	if (!ret && mode == 3)
		ret = cci_multi_reg_write(s->regmap, imx351_sq_regs, ARRAY_SIZE(imx351_sq_regs), NULL);
	/*
	 * Android's square table crops at offset 0,0 = the array's TOP-LEFT 2784x2784 (CamX re-centres it elsewhere); on
	 * the Duo a third of that window lies outside the lens image circle (flat field 2026-11-07: black over 25 % x 75 %
	 * of a 1080p stream, which libcamera scales from this mode). Centre it: (4656 - 2784) / 2, (3496 - 2784) / 2,
	 * matching the crop rectangle get_selection reports.
	 */
	if (!ret && mode == 3)
		ret = cci_write(s->regmap, CCI_REG16(0x0408), 936, NULL);
	if (!ret && mode == 3)
		ret = cci_write(s->regmap, CCI_REG16(0x040a), 356, NULL);
#endif
#ifdef IMX351_WIDE_REGS_PRESENT
	if (!ret && mode == 4)
		ret = cci_multi_reg_write(s->regmap, imx351_wide_regs, ARRAY_SIZE(imx351_wide_regs), NULL);
#endif
#ifdef IMX351_HS_REGS_PRESENT
	if (!ret && mode == 5)
		ret = cci_multi_reg_write(s->regmap, imx351_hs_regs, ARRAY_SIZE(imx351_hs_regs), NULL);
#endif
	if (!ret)
		s->streaming = true;
	if (!ret)
		ret = __v4l2_ctrl_handler_setup(&s->ctrls);	/* exposure / gain / frame length */
	if (!ret)
		ret = cci_write(s->regmap, IMX351_REG_MODE_SELECT, 1, NULL);
	if (!ret)	/* Android writes TEMP_SENS_CTL = 1 right after stream on */
		ret = cci_write(s->regmap, CCI_REG8(0x0138), 1, NULL);
	if (ret) {
		dev_err(s->dev, "stream on failed: %d\n", ret);
		pm_runtime_put(s->dev);
	}
	return ret;
}

static int imx351_disable_streams(struct v4l2_subdev *sd, struct v4l2_subdev_state *state, u32 pad, u64 streams)
{
	struct imx351 *s = to_imx351(sd);

	cci_write(s->regmap, IMX351_REG_MODE_SELECT, 0, NULL);
	s->streaming = false;
	pm_runtime_put(s->dev);
	return 0;
}

/* the modes this build can stream (2..5 need their delta arrays in the local camx header) */
static const int imx351_modes[] = {
	2,	/* 3712x2784 4:3 - listed first: Android's default photo framing */
#ifndef IMX351_43_REGS_PRESENT
#error "imx351_43_regs missing: generate the local header with camera/camx-mode-delta.py"
#endif
#ifdef IMX351_WIDE_REGS_PRESENT
	4,	/* 4032x2784 */
#endif
	0,	/* 4032x2268 16:9 */
#ifdef IMX351_SQ_REGS_PRESENT
	3,	/* 2784x2784 1:1 */
#endif
	1,	/* 1920x1080 */
};
static int mode_w(int m) { int o = mode, r; mode = m; r = IMX351_WIDTH; mode = o; return r; }
static int mode_h(int m) { int o = mode, r; mode = m; r = IMX351_HEIGHT; mode = o; return r; }

/* switch the current mode and keep the timing controls consistent with it (not while streaming) */
static void imx351_apply_mode(struct imx351 *s, int m)
{
	if (m == mode || s->streaming)
		return;
	mode = m;
	__v4l2_ctrl_modify_range(s->vblank, 20, 0xffff - IMX351_HEIGHT, 1, IMX351_FLL - IMX351_HEIGHT);
	__v4l2_ctrl_s_ctrl(s->vblank, IMX351_FLL - IMX351_HEIGHT);
	if (s->hblank)
		__v4l2_ctrl_modify_range(s->hblank, IMX351_LLP - IMX351_WIDTH, IMX351_LLP - IMX351_WIDTH, 1,
					 IMX351_LLP - IMX351_WIDTH);
	__v4l2_ctrl_modify_range(s->exposure, 1, IMX351_FLL - IMX351_EXP_MARGIN, 1, IMX351_EXP_DEF);
	dev_info(s->dev, "mode %d: %dx%d\n", mode, IMX351_WIDTH, IMX351_HEIGHT);
}

static int imx351_enum_mbus_code(struct v4l2_subdev *sd, struct v4l2_subdev_state *state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	return 0;
}

static int imx351_enum_frame_size(struct v4l2_subdev *sd, struct v4l2_subdev_state *state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(imx351_modes) || fse->code != MEDIA_BUS_FMT_SBGGR10_1X10)
		return -EINVAL;
	fse->min_width = fse->max_width = mode_w(imx351_modes[fse->index]);
	fse->min_height = fse->max_height = mode_h(imx351_modes[fse->index]);
	return 0;
}

static int imx351_set_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_state *state, struct v4l2_subdev_format *fmt)
{
	/* nearest mode: exact size first, else the smallest |dw| + |dh|; mode 5 (same size as 1) is kept if selected by
	 * the parameter and 1920x1080 is asked for */
	int i, best = mode, bd = INT_MAX;

	for (i = 0; i < ARRAY_SIZE(imx351_modes); i++) {
		int m = imx351_modes[i];
		int d = abs(mode_w(m) - (int)fmt->format.width) + abs(mode_h(m) - (int)fmt->format.height);

		if (d < bd) { bd = d; best = m; }
	}
	if (best == 1 && mode == 5)
		best = 5;
	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		imx351_apply_mode(to_imx351(sd), best);	/* no-op while streaming: then the current mode is reported */
		imx351_fill_fmt(&fmt->format);
	} else {
		int o = mode; mode = best; imx351_fill_fmt(&fmt->format); mode = o;
	}
	*v4l2_subdev_state_get_format(state, 0) = fmt->format;
	return 0;
}

static int imx351_init_state(struct v4l2_subdev *sd, struct v4l2_subdev_state *state)
{
	imx351_fill_fmt(v4l2_subdev_state_get_format(state, 0));
	return 0;
}

/*
 * Pixel array 4656x3496 (16 MP). From the Android mode registers: readout X 0..4655, Y 612..2879 (0x0344..0x034b), then
 * a digital crop of 312 columns per side (0x0408) -> 4032x2268. mode=1 bins that window 2x2 and crops the centre
 * 1920x1080 -> 3840x2160 of the array. libcamera asks for these (it logged "Unable to get rectangle" without them).
 */
#define IMX351_NATIVE_W		4656
#define IMX351_NATIVE_H		3496
static int imx351_get_selection(struct v4l2_subdev *sd, struct v4l2_subdev_state *state,
				struct v4l2_subdev_selection *sel)
{
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		sel->r = mode == 1 ? (struct v4l2_rect){ 408, 666, 3840, 2160 } :
			 mode == 2 ? (struct v4l2_rect){ 472, 356, 3712, 2784 } :	/* centred 4:3 of the full array */
			 mode == 3 ? (struct v4l2_rect){ 936, 356, 2784, 2784 } :	/* centred 1:1 */
			 mode == 4 ? (struct v4l2_rect){ 312, 356, 4032, 2784 } :	/* wide */
			 mode == 5 ? (struct v4l2_rect){ 408, 666, 3840, 2160 } :	/* 1080p high speed */
				     (struct v4l2_rect){ 312, 612, IMX351_FULL_W, IMX351_FULL_H };
		return 0;
	case V4L2_SEL_TGT_NATIVE_SIZE:
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r = (struct v4l2_rect){ 0, 0, IMX351_NATIVE_W, IMX351_NATIVE_H };
		return 0;
	}
	return -EINVAL;
}

static const struct v4l2_subdev_video_ops imx351_video_ops = {
	.s_stream = v4l2_subdev_s_stream_helper,
};

static const struct v4l2_subdev_pad_ops imx351_pad_ops = {
	.enum_mbus_code = imx351_enum_mbus_code,
	.enum_frame_size = imx351_enum_frame_size,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = imx351_set_fmt,
	.get_selection = imx351_get_selection,
	.enable_streams = imx351_enable_streams,
	.disable_streams = imx351_disable_streams,
};

static const struct v4l2_subdev_ops imx351_ops = {
	.video = &imx351_video_ops,
	.pad = &imx351_pad_ops,
};

static const struct v4l2_subdev_internal_ops imx351_internal_ops = {
	.init_state = imx351_init_state,
};

static int imx351_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct imx351 *s;
	struct v4l2_ctrl *lf;
	u64 id = 0;
	int ret;

	s = devm_kzalloc(dev, sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;
	s->dev = dev;
	s->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(s->regmap))
		return PTR_ERR(s->regmap);
	s->xclk = devm_clk_get(dev, NULL);
	if (IS_ERR(s->xclk))
		return dev_err_probe(dev, PTR_ERR(s->xclk), "no xclk\n");
	ret = clk_set_rate(s->xclk, IMX351_XCLK);
	if (ret)
		return dev_err_probe(dev, ret, "xclk rate\n");
	s->vio = devm_regulator_get(dev, "vio");
	if (IS_ERR(s->vio))
		return PTR_ERR(s->vio);
	s->vana = devm_regulator_get_optional(dev, "vana");
	if (IS_ERR(s->vana)) {
		if (PTR_ERR(s->vana) != -ENODEV)
			return dev_err_probe(dev, PTR_ERR(s->vana), "vana supply\n");
		s->vana = NULL;
	}
	/* either power-gpios (switch driven directly) or a vana regulator on the same switch */
	s->power = devm_gpiod_get_optional(dev, "power", GPIOD_OUT_LOW);
	if (IS_ERR(s->power))
		return dev_err_probe(dev, PTR_ERR(s->power), "no power gpio\n");
	s->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(s->reset))
		return dev_err_probe(dev, PTR_ERR(s->reset), "no reset gpio\n");

	v4l2_i2c_subdev_init(&s->sd, client, &imx351_ops);
	s->sd.internal_ops = &imx351_internal_ops;

	ret = imx351_power_on(dev);
	if (ret)
		return ret;
	ret = cci_read(s->regmap, IMX351_REG_MODEL_ID, &id, NULL);
	if (ret || id != IMX351_MODEL_ID) {
		dev_err(dev, "model id %#llx (ret %d), expected %#x\n", id, ret, IMX351_MODEL_ID);
		ret = ret ?: -ENODEV;
		goto err_power;
	}
	dev_info(dev, "Sony IMX351 found (model id %#llx)\n", id);

	v4l2_ctrl_handler_init(&s->ctrls, 8);
	lf = v4l2_ctrl_new_int_menu(&s->ctrls, NULL, V4L2_CID_LINK_FREQ, 0, 0, imx351_link_freqs);
	if (lf)
		lf->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	v4l2_ctrl_new_std(&s->ctrls, NULL, V4L2_CID_PIXEL_RATE, IMX351_PIXEL_RATE, IMX351_PIXEL_RATE, 1,
			  IMX351_PIXEL_RATE);
	s->vblank = v4l2_ctrl_new_std(&s->ctrls, &imx351_ctrl_ops, V4L2_CID_VBLANK, 20, 0xffff - IMX351_HEIGHT, 1,
				      IMX351_FLL - IMX351_HEIGHT);
	{
		s->hblank = v4l2_ctrl_new_std(&s->ctrls, NULL, V4L2_CID_HBLANK, IMX351_LLP - IMX351_WIDTH,
					      IMX351_LLP - IMX351_WIDTH, 1, IMX351_LLP - IMX351_WIDTH);
		if (s->hblank)
			s->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	}
	s->exposure = v4l2_ctrl_new_std(&s->ctrls, &imx351_ctrl_ops, V4L2_CID_EXPOSURE, 1,
					IMX351_FLL - IMX351_EXP_MARGIN, 1, IMX351_EXP_DEF);
	v4l2_ctrl_new_std(&s->ctrls, &imx351_ctrl_ops, V4L2_CID_ANALOGUE_GAIN, 0, IMX351_GAIN_MAX, 1, IMX351_GAIN_DEF);
	{
		/* DT orientation/rotation -> V4L2_CID_CAMERA_ORIENTATION/_SENSOR_ROTATION (libcamera asks for both) */
		struct v4l2_fwnode_device_properties props;

		ret = v4l2_fwnode_device_parse(dev, &props);
		if (!ret)
			v4l2_ctrl_new_fwnode_properties(&s->ctrls, &imx351_ctrl_ops, &props);
	}
	if (s->ctrls.error) {
		ret = s->ctrls.error;
		goto err_ctrls;
	}
	s->sd.ctrl_handler = &s->ctrls;

	s->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	s->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	s->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&s->sd.entity, 1, &s->pad);
	if (ret)
		goto err_ctrls;
	s->sd.state_lock = s->ctrls.lock;
	ret = v4l2_subdev_init_finalize(&s->sd);
	if (ret)
		goto err_entity;

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	ret = v4l2_async_register_subdev_sensor(&s->sd);
	if (ret)
		goto err_pm;
	return 0;

err_pm:
	pm_runtime_disable(dev);
	v4l2_subdev_cleanup(&s->sd);
err_entity:
	media_entity_cleanup(&s->sd.entity);
err_ctrls:
	v4l2_ctrl_handler_free(&s->ctrls);
err_power:
	imx351_power_off(dev);
	return ret;
}

static void imx351_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx351 *s = to_imx351(sd);

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(&s->ctrls);
	pm_runtime_disable(s->dev);
	if (!pm_runtime_status_suspended(s->dev))
		imx351_power_off(s->dev);
	pm_runtime_set_suspended(s->dev);
}

static DEFINE_RUNTIME_DEV_PM_OPS(imx351_pm_ops, imx351_power_off, imx351_power_on, NULL);

static const struct of_device_id imx351_of_match[] = {
	{ .compatible = "sony,imx351" },
	{ }
};
MODULE_DEVICE_TABLE(of, imx351_of_match);

static struct i2c_driver imx351_driver = {
	.driver = {
		.name = "imx351",
		.of_match_table = imx351_of_match,
		.pm = pm_ptr(&imx351_pm_ops),
	},
	.probe = imx351_probe,
	.remove = imx351_remove,
};
module_i2c_driver(imx351_driver);

MODULE_DESCRIPTION("Sony IMX351 minimal sensor driver (Surface Duo workbench)");
MODULE_LICENSE("GPL");
