// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung Galaxy Tab 3 8.0 (SM-T311) SR130PC20 hardware bring-up driver.
 *
 * v206 extends the already-proven v131 hardware identification path:
 * - exact T311 rails, nSTBY/nRST and 24 MHz MCLK
 * - page 0 register 0x04 must read 0xc1
 * - modern Linux 6.12 V4L2 async sensor entity
 * - exact CM14.1 SM-T311 VGA init/preview/stream tables
 *
 * CameraProvider remains intentionally disabled until raw FIMC capture is proven.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>

#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-async.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>

#include "t311-sr130pc20-regs.h"
#include "t311-sr130pc20-full-regs.h"

#define T311_SR130PC20_CHIP_ID  0xc1
#define T311_SR130PC20_XCLK     24000000UL
#define V4L2_CID_T311_FAST_RECONFIG (V4L2_CID_USER_BASE + 0x11f0)

struct t311_sr130pc20_mode { u16 width, height; bool capture; const u16 *regs; size_t count; };
#define T311_SR_MODE(w,h,c,t) { (w),(h),(c),(t),ARRAY_SIZE(t) }
static const struct t311_sr130pc20_mode t311_sr130pc20_modes[] = {
	T311_SR_MODE(320,240,false,sr130pc20_320_240_Preview),
	T311_SR_MODE(352,288,false,sr130pc20_352_288_Preview),
	T311_SR_MODE(640,360,false,sr130pc20_640_360_Preview),
	T311_SR_MODE(640,480,false,sr130pc20_640_480_Preview),
	T311_SR_MODE(1280,720,true,SR130PC20_1280_720_Capture_Size),
	T311_SR_MODE(1280,960,true,SR130PC20_1280_960_Capture_Size),
};

struct t311_sr130pc20 {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct mutex lock;
	struct device *dev;

	struct regulator *core;
	struct regulator *io;
	struct regulator *analog;
	struct regulator *vt;
	struct clk *xclk;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_mclk_active;
	struct pinctrl_state *pins_mclk_idle;
	struct gpio_desc *standby;
	struct gpio_desc *reset;

	struct v4l2_mbus_framefmt fmt;
	struct v4l2_ctrl_handler ctrls;
	struct v4l2_ctrl *iso_auto;
	struct v4l2_ctrl *iso;
	u32 fps;
	u32 fast_target_w;
	u32 fast_target_h;
	bool fast_reconfig_pending;
	bool fast_reconfig_armed;
	bool powered;
	bool initialized;
	bool streaming;
};

static inline struct t311_sr130pc20 *to_t311_sr130pc20(struct v4l2_subdev *sd)
{
	return container_of(sd, struct t311_sr130pc20, sd);
}

static int t311_sr130pc20_write8(struct t311_sr130pc20 *cam, u8 reg, u8 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&cam->sd);
	u8 buf[2] = { reg, val };
	int ret = -EIO;
	int retry;

	/* Stock Samsung driver retries a failed I2C write up to five times. */
	for (retry = 0; retry < 5; retry++) {
		ret = i2c_master_send(client, buf, sizeof(buf));
		if (ret == sizeof(buf))
			return 0;
		if (ret >= 0)
			ret = -EIO;
		msleep(10);
	}

	return ret;
}

static int t311_sr130pc20_write_table(struct t311_sr130pc20 *cam,
		const u16 *regs, size_t count, const char *name)
{
	size_t i;
	int ret;

	for (i = 0; i < count; i++) {
		u8 reg = regs[i] >> 8;
		u8 val = regs[i] & 0xff;

		/* Keep compatibility with Samsung setfiles if delays are added later. */
		if (reg == 0xff) {
			msleep((unsigned int)val * 10);
			continue;
		}

		ret = t311_sr130pc20_write8(cam, reg, val);
		if (ret) {
			dev_err(cam->dev,
				"SR130PC20: %s failed at %zu/%zu reg=0x%02x val=0x%02x: %d\n",
				name, i + 1, count, reg, val, ret);
			return ret;
		}
	}

	return 0;
}

static int t311_sr130pc20_read8(struct t311_sr130pc20 *cam, u8 reg, u8 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&cam->sd);
	struct i2c_msg msgs[] = {
		{ .addr = client->addr, .flags = 0, .len = 1, .buf = &reg },
		{ .addr = client->addr, .flags = I2C_M_RD, .len = 1, .buf = val },
	};
	int ret;

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret == ARRAY_SIZE(msgs))
		return 0;
	return ret < 0 ? ret : -EIO;
}

static void t311_sr130pc20_q35o_capture_readback(struct t311_sr130pc20 *cam)
{
	u8 frame_mode = 0xff;
	u8 sleep = 0xff, pll0 = 0xff, pll1 = 0xff, pll2 = 0xff, pll3 = 0xff;
	u8 p18_10 = 0xff, p18_14 = 0xff;
	u8 m10 = 0xff, m1d = 0xff, m1e = 0xff, m1f = 0xff;
	u8 m30 = 0xff, m31 = 0xff, m32 = 0xff, m33 = 0xff;
	u8 m34 = 0xff, m35 = 0xff, m36 = 0xff, m37 = 0xff;
	int err = 0;

	/* Diagnostic only: read back the exact state that distinguishes VGA
	 * preview from 1MP capture. Always restore page 0 before returning. */
	err |= t311_sr130pc20_write8(cam, 0x03, 0x00);
	err |= t311_sr130pc20_read8(cam, 0x01, &sleep);
	err |= t311_sr130pc20_read8(cam, 0x11, &frame_mode);
	err |= t311_sr130pc20_read8(cam, 0xd0, &pll0);
	err |= t311_sr130pc20_read8(cam, 0xd1, &pll1);
	err |= t311_sr130pc20_read8(cam, 0xd2, &pll2);
	err |= t311_sr130pc20_read8(cam, 0xd3, &pll3);
	err |= t311_sr130pc20_write8(cam, 0x03, 0x18);
	err |= t311_sr130pc20_read8(cam, 0x10, &p18_10);
	err |= t311_sr130pc20_read8(cam, 0x14, &p18_14);
	err |= t311_sr130pc20_write8(cam, 0x03, 0x48);
	err |= t311_sr130pc20_read8(cam, 0x10, &m10);
	err |= t311_sr130pc20_read8(cam, 0x1d, &m1d);
	err |= t311_sr130pc20_read8(cam, 0x1e, &m1e);
	err |= t311_sr130pc20_read8(cam, 0x1f, &m1f);
	err |= t311_sr130pc20_read8(cam, 0x30, &m30);
	err |= t311_sr130pc20_read8(cam, 0x31, &m31);
	err |= t311_sr130pc20_read8(cam, 0x32, &m32);
	err |= t311_sr130pc20_read8(cam, 0x33, &m33);
	err |= t311_sr130pc20_read8(cam, 0x34, &m34);
	err |= t311_sr130pc20_read8(cam, 0x35, &m35);
	err |= t311_sr130pc20_read8(cam, 0x36, &m36);
	err |= t311_sr130pc20_read8(cam, 0x37, &m37);
	t311_sr130pc20_write8(cam, 0x03, 0x00);

	dev_info(cam->dev,
		 "SMT311_Q35O_CAPTURE_READBACK err=%d sleep=%02x pll=%02x/%02x/%02x/%02x p18_10=%02x p18_14=%02x mipi10=%02x mipi1d=%02x mipi1e=%02x mipi1f=%02x size=%02x/%02x timing=%02x/%02x/%02x/%02x/%02x/%02x\n",
		 err, sleep, pll0, pll1, pll2, pll3, p18_10, p18_14,
		 m10, m1d, m1e, m1f, m30, m31, m32, m33, m34, m35, m36, m37);
	dev_info(cam->dev, "SMT311_Q35P_CAPTURE_TIMING frame_mode=%02x fixed_fps=%u err=%d\n",
		 frame_mode, !!(frame_mode & 0x04), err);
}

static int t311_sr130pc20_power_on(struct t311_sr130pc20 *cam)
{
	int ret;

	if (cam->powered)
		return 0;

	dev_info(cam->dev,
		 "PJARCZAK CAMERA HOTFIX11: front POWER PRE core=%d io=%d analog=%d vt=%d xclk=%lu\n",
		 regulator_is_enabled(cam->core), regulator_is_enabled(cam->io),
		 regulator_is_enabled(cam->analog), regulator_is_enabled(cam->vt),
		 clk_get_rate(cam->xclk));

	/* Exact T311: nSTBY and nRST asserted while rails settle. */
	gpiod_set_value_cansleep(cam->standby, 1);
	gpiod_set_value_cansleep(cam->reset, 1);

	ret = regulator_enable(cam->core);
	if (ret)
		return ret;
	udelay(10);

	ret = regulator_enable(cam->io);
	if (ret)
		goto err_core;
	udelay(10);

	ret = regulator_enable(cam->analog);
	if (ret)
		goto err_io;
	udelay(10);

	ret = regulator_enable(cam->vt);
	if (ret)
		goto err_analog;
	usleep_range(300, 310);

	/* Physical nSTBY high => logical inactive for GPIO_ACTIVE_LOW. */
	gpiod_set_value_cansleep(cam->standby, 0);
	udelay(20);

	ret = pinctrl_select_state(cam->pinctrl, cam->pins_mclk_active);
	if (ret)
		goto err_vt;
	dev_dbg(cam->dev, "PJARCZAK CAMERA HOTFIX10: front MCLK pin ACTIVE\n");
	ret = clk_set_rate(cam->xclk, T311_SR130PC20_XCLK);
	if (ret)
		goto err_vt;
	ret = clk_prepare_enable(cam->xclk);
	if (ret)
		goto err_vt;

	usleep_range(11000, 12000);

	/* Physical nRST high => release reset. */
	gpiod_set_value_cansleep(cam->reset, 0);
	usleep_range(1100, 1200);

	cam->powered = true;
	dev_info(cam->dev,
		 "PJARCZAK CAMERA HOTFIX11: front POWER ON core=%d io=%d analog=%d vt=%d xclk=%lu reset=%d standby=%d\n",
		 regulator_is_enabled(cam->core), regulator_is_enabled(cam->io),
		 regulator_is_enabled(cam->analog), regulator_is_enabled(cam->vt),
		 clk_get_rate(cam->xclk), gpiod_get_value_cansleep(cam->reset),
		 gpiod_get_value_cansleep(cam->standby));
	return 0;

err_vt:
	pinctrl_select_state(cam->pinctrl, cam->pins_mclk_idle);
	gpiod_set_value_cansleep(cam->standby, 1);
	regulator_disable(cam->vt);
err_analog:
	regulator_disable(cam->analog);
err_io:
	regulator_disable(cam->io);
err_core:
	regulator_disable(cam->core);
	return ret;
}

static void t311_sr130pc20_power_off(struct t311_sr130pc20 *cam)
{
	cam->fast_reconfig_pending = false;
	cam->fast_reconfig_armed = false;
	cam->fast_target_w = cam->fast_target_h = 0;
	if (!cam->powered)
		return;

	gpiod_set_value_cansleep(cam->reset, 1);
	usleep_range(1000, 1100);
	clk_disable_unprepare(cam->xclk);
	pinctrl_select_state(cam->pinctrl, cam->pins_mclk_idle);
	dev_dbg(cam->dev, "PJARCZAK CAMERA HOTFIX10: front MCLK pin IDLE\n");
	udelay(50);
	gpiod_set_value_cansleep(cam->standby, 1);

	regulator_disable(cam->vt);
	usleep_range(500, 510);
	regulator_disable(cam->analog);
	udelay(5);
	regulator_disable(cam->io);
	udelay(5);
	regulator_disable(cam->core);
	cam->powered = false;
	cam->initialized = false;
	cam->streaming = false;
	dev_info(cam->dev,
		 "PJARCZAK CAMERA HOTFIX11: front POWER OFF core=%d io=%d analog=%d vt=%d xclk=%lu reset=%d standby=%d\n",
		 regulator_is_enabled(cam->core), regulator_is_enabled(cam->io),
		 regulator_is_enabled(cam->analog), regulator_is_enabled(cam->vt),
		 clk_get_rate(cam->xclk), gpiod_get_value_cansleep(cam->reset),
		 gpiod_get_value_cansleep(cam->standby));
}

static int t311_sr130pc20_detect(struct t311_sr130pc20 *cam)
{
	u8 chip = 0;
	int ret;

	ret = t311_sr130pc20_write8(cam, 0x03, 0x00);
	if (ret)
		return dev_err_probe(cam->dev, ret,
				     "SR130PC20: failed selecting page 0\n");

	ret = t311_sr130pc20_read8(cam, 0x04, &chip);
	if (ret)
		return dev_err_probe(cam->dev, ret,
				     "SR130PC20: chip-id I2C read failed\n");

	if (chip != T311_SR130PC20_CHIP_ID) {
		dev_err(cam->dev,
			"SR130PC20: unexpected chip id 0x%02x (expected 0x%02x)\n",
			chip, T311_SR130PC20_CHIP_ID);
		return -ENODEV;
	}

	dev_info(cam->dev,
		 "PJARCZAK CAMERA: SR130PC20 detected, chip-id=0x%02x, xclk=%lu\n",
		 chip, T311_SR130PC20_XCLK);
	return 0;
}

static const struct t311_sr130pc20_mode *t311_sr130pc20_find_mode(u32 w,u32 h)
{
	unsigned int i;
	for(i=0;i<ARRAY_SIZE(t311_sr130pc20_modes);i++)
		if(t311_sr130pc20_modes[i].width==w && t311_sr130pc20_modes[i].height==h) return &t311_sr130pc20_modes[i];
	return &t311_sr130pc20_modes[3];
}

static int t311_sr130pc20_apply(struct t311_sr130pc20 *cam,const u16 *t,size_t n,const char *name)
{
	if(!cam->powered || !cam->initialized) return 0;
	return t311_sr130pc20_write_table(cam,t,n,name);
}
static int t311_sr130pc20_apply_iso(struct t311_sr130pc20 *cam,int iso)
{
	if (!cam->powered || !cam->initialized) return 0;
	if (cam->iso_auto && cam->iso_auto->val==V4L2_ISO_SENSITIVITY_AUTO) return t311_sr130pc20_apply(cam,sr130pc20_ISO_Auto,ARRAY_SIZE(sr130pc20_ISO_Auto),"iso-auto");
	if (iso<=50) return t311_sr130pc20_apply(cam,sr130pc20_ISO_50,ARRAY_SIZE(sr130pc20_ISO_50),"iso50");
	if (iso<=100) return t311_sr130pc20_apply(cam,sr130pc20_ISO_100,ARRAY_SIZE(sr130pc20_ISO_100),"iso100");
	if (iso<=200) return t311_sr130pc20_apply(cam,sr130pc20_ISO_200,ARRAY_SIZE(sr130pc20_ISO_200),"iso200");
	return t311_sr130pc20_apply(cam,sr130pc20_ISO_400,ARRAY_SIZE(sr130pc20_ISO_400),"iso400");
}




static const s64 t311_sr130pc20_iso_qmenu[] = { 50, 100, 200, 400 };

static int t311_sr130pc20_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct t311_sr130pc20 *cam=container_of(ctrl->handler,struct t311_sr130pc20,ctrls);
	int v=ctrl->val;
	if (ctrl->id == V4L2_CID_T311_FAST_RECONFIG) {
		u32 w = ((u32)v >> 16) & 0xffff;
		u32 h = (u32)v & 0xffff;
		bool valid = (w == 1280 && (h == 960 || h == 720)) ||
			(w == 640 && (h == 480 || h == 360)) ||
			(w == 352 && h == 288) || (w == 320 && h == 240);
		if (v == 0) {
			cam->fast_reconfig_pending = false;
			cam->fast_reconfig_armed = false;
			cam->fast_target_w = cam->fast_target_h = 0;
			dev_info(cam->dev, "SMT311_Q35B_FAST_RECONFIG_CLEAR\n");
			return 0;
		}
		if (!valid)
			return -EINVAL;
		if (!cam->powered || !cam->initialized || !cam->streaming) {
			cam->fast_reconfig_pending = false;
			cam->fast_reconfig_armed = false;
			cam->fast_target_w = cam->fast_target_h = 0;
			dev_info(cam->dev, "SMT311_Q35J_FAST_RECONFIG_STALE_REPLAY_CLEAR value=%ux%u\n", w, h);
			return 0;
		}
		cam->fast_target_w = w;
		cam->fast_target_h = h;
		cam->fast_reconfig_pending = true;
		cam->fast_reconfig_armed = false;
		dev_info(cam->dev, "SMT311_Q35B_FAST_RECONFIG_REQUEST %ux%u from=%ux%u fps=%u\n",
			w, h, cam->fmt.width, cam->fmt.height, cam->fps);
		return 0;
	}
	if(!cam->powered || !cam->initialized) return 0;
	switch(ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		if(v<=-4) return t311_sr130pc20_apply(cam,SR130PC20_ExpSetting_M4Step,ARRAY_SIZE(SR130PC20_ExpSetting_M4Step),"ev-4");
		if(v==-3) return t311_sr130pc20_apply(cam,SR130PC20_ExpSetting_M3Step,ARRAY_SIZE(SR130PC20_ExpSetting_M3Step),"ev-3");
		if(v==-2) return t311_sr130pc20_apply(cam,SR130PC20_ExpSetting_M2Step,ARRAY_SIZE(SR130PC20_ExpSetting_M2Step),"ev-2");
		if(v==-1) return t311_sr130pc20_apply(cam,SR130PC20_ExpSetting_M1Step,ARRAY_SIZE(SR130PC20_ExpSetting_M1Step),"ev-1");
		if(v==1) return t311_sr130pc20_apply(cam,SR130PC20_ExpSetting_P1Step,ARRAY_SIZE(SR130PC20_ExpSetting_P1Step),"ev+1");
		if(v==2) return t311_sr130pc20_apply(cam,SR130PC20_ExpSetting_P2Step,ARRAY_SIZE(SR130PC20_ExpSetting_P2Step),"ev+2");
		if(v==3) return t311_sr130pc20_apply(cam,SR130PC20_ExpSetting_P3Step,ARRAY_SIZE(SR130PC20_ExpSetting_P3Step),"ev+3");
		if(v>=4) return t311_sr130pc20_apply(cam,SR130PC20_ExpSetting_P4Step,ARRAY_SIZE(SR130PC20_ExpSetting_P4Step),"ev+4");
		return t311_sr130pc20_apply(cam,SR130PC20_ExpSetting_Default,ARRAY_SIZE(SR130PC20_ExpSetting_Default),"ev0");
	case V4L2_CID_ISO_SENSITIVITY_AUTO:
	case V4L2_CID_ISO_SENSITIVITY: return t311_sr130pc20_apply_iso(cam,cam->iso?t311_sr130pc20_iso_qmenu[cam->iso->val]:100);
	case V4L2_CID_EXPOSURE_METERING:
		if(v==V4L2_EXPOSURE_METERING_CENTER_WEIGHTED) return t311_sr130pc20_apply(cam,sr130pc20_Metering_Center,ARRAY_SIZE(sr130pc20_Metering_Center),"meter-center");
		if(v==V4L2_EXPOSURE_METERING_SPOT) return t311_sr130pc20_apply(cam,sr130pc20_Metering_Spot,ARRAY_SIZE(sr130pc20_Metering_Spot),"meter-spot");
		return t311_sr130pc20_apply(cam,sr130pc20_Metering_Matrix,ARRAY_SIZE(sr130pc20_Metering_Matrix),"meter-matrix");
	case V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE:
		if(v==V4L2_WHITE_BALANCE_DAYLIGHT) return t311_sr130pc20_apply(cam,sr130pc20_WB_Sunny,ARRAY_SIZE(sr130pc20_WB_Sunny),"wb-daylight");
		if(v==V4L2_WHITE_BALANCE_CLOUDY) return t311_sr130pc20_apply(cam,sr130pc20_WB_Cloudy,ARRAY_SIZE(sr130pc20_WB_Cloudy),"wb-cloudy");
		if(v==V4L2_WHITE_BALANCE_INCANDESCENT) return t311_sr130pc20_apply(cam,sr130pc20_WB_Tungsten,ARRAY_SIZE(sr130pc20_WB_Tungsten),"wb-tungsten");
		if(v==V4L2_WHITE_BALANCE_FLUORESCENT) return t311_sr130pc20_apply(cam,sr130pc20_WB_Fluorescent,ARRAY_SIZE(sr130pc20_WB_Fluorescent),"wb-fluor");
		return t311_sr130pc20_apply(cam,sr130pc20_WB_Auto,ARRAY_SIZE(sr130pc20_WB_Auto),"wb-auto");
	case V4L2_CID_COLORFX:
		if(v==V4L2_COLORFX_BW) return t311_sr130pc20_apply(cam,sr130pc20_Effect_Black_White,ARRAY_SIZE(sr130pc20_Effect_Black_White),"fx-bw");
		if(v==V4L2_COLORFX_SEPIA) return t311_sr130pc20_apply(cam,sr130pc20_Effect_Sepia,ARRAY_SIZE(sr130pc20_Effect_Sepia),"fx-sepia");
		if(v==V4L2_COLORFX_NEGATIVE) return t311_sr130pc20_apply(cam,SR130PC20_Effect_Negative,ARRAY_SIZE(SR130PC20_Effect_Negative),"fx-neg");
		if(v==V4L2_COLORFX_SKETCH) return t311_sr130pc20_apply(cam,sr130pc20_Effect_Sketch,ARRAY_SIZE(sr130pc20_Effect_Sketch),"fx-sketch");
		if(v==V4L2_COLORFX_SOLARIZATION) return t311_sr130pc20_apply(cam,sr130pc20_Effect_Solar,ARRAY_SIZE(sr130pc20_Effect_Solar),"fx-solar");
		return t311_sr130pc20_apply(cam,sr130pc20_Effect_Normal,ARRAY_SIZE(sr130pc20_Effect_Normal),"fx-none");
	case V4L2_CID_SCENE_MODE:
		switch(v) {
		case V4L2_SCENE_MODE_BACKLIGHT:return t311_sr130pc20_apply(cam,sr130pc20_Scene_Backlight,ARRAY_SIZE(sr130pc20_Scene_Backlight),"scene-backlight");
		case V4L2_SCENE_MODE_BEACH_SNOW:return t311_sr130pc20_apply(cam,sr130pc20_Scene_Beach_Snow,ARRAY_SIZE(sr130pc20_Scene_Beach_Snow),"scene-beach");
		case V4L2_SCENE_MODE_CANDLE_LIGHT:return t311_sr130pc20_apply(cam,sr130pc20_Scene_Candle_Light,ARRAY_SIZE(sr130pc20_Scene_Candle_Light),"scene-candle");
		case V4L2_SCENE_MODE_DAWN_DUSK:return t311_sr130pc20_apply(cam,sr130pc20_Scene_Duskdawn,ARRAY_SIZE(sr130pc20_Scene_Duskdawn),"scene-dusk");
		case V4L2_SCENE_MODE_FALL_COLORS:return t311_sr130pc20_apply(cam,sr130pc20_Scene_Fall_Color,ARRAY_SIZE(sr130pc20_Scene_Fall_Color),"scene-fall");
		case V4L2_SCENE_MODE_FIREWORKS:return t311_sr130pc20_apply(cam,sr130pc20_Scene_Fireworks,ARRAY_SIZE(sr130pc20_Scene_Fireworks),"scene-fireworks");
		case V4L2_SCENE_MODE_LANDSCAPE:return t311_sr130pc20_apply(cam,sr130pc20_Scene_Landscape,ARRAY_SIZE(sr130pc20_Scene_Landscape),"scene-landscape");
		case V4L2_SCENE_MODE_NIGHT:return t311_sr130pc20_apply(cam,sr130pc20_Scene_Nightshot,ARRAY_SIZE(sr130pc20_Scene_Nightshot),"scene-night");
		case V4L2_SCENE_MODE_PARTY_INDOOR:return t311_sr130pc20_apply(cam,sr130pc20_Scene_Party_Indoor,ARRAY_SIZE(sr130pc20_Scene_Party_Indoor),"scene-party");
		case V4L2_SCENE_MODE_PORTRAIT:return t311_sr130pc20_apply(cam,sr130pc20_Scene_Portrait,ARRAY_SIZE(sr130pc20_Scene_Portrait),"scene-portrait");
		case V4L2_SCENE_MODE_SPORTS:return t311_sr130pc20_apply(cam,sr130pc20_Scene_Sports,ARRAY_SIZE(sr130pc20_Scene_Sports),"scene-sports");
		case V4L2_SCENE_MODE_SUNSET:return t311_sr130pc20_apply(cam,sr130pc20_Scene_Sunset,ARRAY_SIZE(sr130pc20_Scene_Sunset),"scene-sunset");
		case V4L2_SCENE_MODE_TEXT:return t311_sr130pc20_apply(cam,sr130pc20_Scene_Text,ARRAY_SIZE(sr130pc20_Scene_Text),"scene-text");
		default:return t311_sr130pc20_apply(cam,sr130pc20_Scene_Default,ARRAY_SIZE(sr130pc20_Scene_Default),"scene-none");
		}
	default:return 0;
	}
}
static const struct v4l2_ctrl_ops t311_sr130pc20_ctrl_ops={.s_ctrl=t311_sr130pc20_s_ctrl};
static const struct v4l2_ctrl_config t311_sr130pc20_fast_reconfig_ctrl = {
	.ops = &t311_sr130pc20_ctrl_ops,
	.id = V4L2_CID_T311_FAST_RECONFIG,
	.name = "T311 SR130 fast reconfigure",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 0x7fffffff,
	.step = 1,
	.def = 0,
	.flags = V4L2_CTRL_FLAG_EXECUTE_ON_WRITE,
};

static int t311_sr130pc20_init_controls(struct t311_sr130pc20 *cam)
{
	struct v4l2_ctrl_handler *h=&cam->ctrls; int ret;
	u64 wb_skip=~((1ULL<<V4L2_WHITE_BALANCE_AUTO)|(1ULL<<V4L2_WHITE_BALANCE_INCANDESCENT)|(1ULL<<V4L2_WHITE_BALANCE_FLUORESCENT)|(1ULL<<V4L2_WHITE_BALANCE_DAYLIGHT)|(1ULL<<V4L2_WHITE_BALANCE_CLOUDY));
	u64 fx_skip=~((1ULL<<V4L2_COLORFX_NONE)|(1ULL<<V4L2_COLORFX_BW)|(1ULL<<V4L2_COLORFX_SEPIA)|(1ULL<<V4L2_COLORFX_NEGATIVE)|(1ULL<<V4L2_COLORFX_SKETCH)|(1ULL<<V4L2_COLORFX_SOLARIZATION));
	ret=v4l2_ctrl_handler_init(h,11); if(ret) return ret;
	h->lock = &cam->lock;
	v4l2_ctrl_new_std(h,&t311_sr130pc20_ctrl_ops,V4L2_CID_BRIGHTNESS,-4,4,1,0);
	v4l2_ctrl_new_std_menu(h,&t311_sr130pc20_ctrl_ops,V4L2_CID_EXPOSURE_METERING,V4L2_EXPOSURE_METERING_MATRIX,0,V4L2_EXPOSURE_METERING_MATRIX);
	cam->iso_auto=v4l2_ctrl_new_std_menu(h,&t311_sr130pc20_ctrl_ops,V4L2_CID_ISO_SENSITIVITY_AUTO,V4L2_ISO_SENSITIVITY_AUTO,0,V4L2_ISO_SENSITIVITY_AUTO);
	cam->iso=v4l2_ctrl_new_int_menu(h,&t311_sr130pc20_ctrl_ops,V4L2_CID_ISO_SENSITIVITY,ARRAY_SIZE(t311_sr130pc20_iso_qmenu)-1,1,t311_sr130pc20_iso_qmenu);
	v4l2_ctrl_new_std_menu(h,&t311_sr130pc20_ctrl_ops,V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE,V4L2_WHITE_BALANCE_CLOUDY,wb_skip,V4L2_WHITE_BALANCE_AUTO);
	v4l2_ctrl_new_std_menu(h,&t311_sr130pc20_ctrl_ops,V4L2_CID_COLORFX,V4L2_COLORFX_SOLARIZATION,fx_skip,V4L2_COLORFX_NONE);
	v4l2_ctrl_new_std_menu(h,&t311_sr130pc20_ctrl_ops,V4L2_CID_SCENE_MODE,V4L2_SCENE_MODE_TEXT,0,V4L2_SCENE_MODE_NONE);
	v4l2_ctrl_new_custom(h, &t311_sr130pc20_fast_reconfig_ctrl, NULL);
	if(h->error){ret=h->error;return ret;} cam->sd.ctrl_handler=h; return 0;
}
static int t311_sr130pc20_get_frame_interval(struct v4l2_subdev *sd,
                                              struct v4l2_subdev_state *state,
                                              struct v4l2_subdev_frame_interval *fi)
{
	struct t311_sr130pc20 *cam = to_t311_sr130pc20(sd);
	(void)state;
	mutex_lock(&cam->lock);
	fi->interval.numerator = 1;
	fi->interval.denominator = cam->fps ? cam->fps : 25;
	mutex_unlock(&cam->lock);
	return 0;
}

static int t311_sr130pc20_set_frame_interval(struct v4l2_subdev *sd,
                                              struct v4l2_subdev_state *state,
                                              struct v4l2_subdev_frame_interval *fi)
{
	struct t311_sr130pc20 *cam = to_t311_sr130pc20(sd);
	const struct t311_sr130pc20_mode *mode;
	u32 fps;
	int ret = 0;
	(void)state;
	ret = 0;
	mutex_lock(&cam->lock);
	mode = t311_sr130pc20_find_mode(cam->fmt.width, cam->fmt.height);
	if (!fi->interval.numerator || !fi->interval.denominator)
		fps = 25;
	else
		fps = DIV_ROUND_CLOSEST(fi->interval.denominator, fi->interval.numerator);
	/* SMT311_Q34Z_SR_STOCK_FPS_CAP: stock T311 clamps >25 fps to 25. */
	if (fps >= 20) fps = 25;
	else if (fps >= 11) fps = 15;
	else fps = 7;
	if (cam->fps == fps) {
		fi->interval.numerator = 1;
		fi->interval.denominator = fps;
		if (mode->capture)
			dev_info(cam->dev,
				 "SMT311_Q35A_SR_CAPTURE_S_PARM_STATE_ONLY fps=%u %ux%u unchanged=1\n",
				 fps, cam->fmt.width, cam->fmt.height);
		goto out;
	}
	/* SMT311_Q35A_SR_CAPTURE_OWNS_TIMING:
	 * Stock Samsung start_capture() does not apply fixed-FPS tables while
	 * entering 1MP still capture. s_parm() may update the requested state,
	 * but the capture setfiles own exposure/PLL/MIPI timing. */
	if (mode->capture) {
		dev_info(cam->dev,
			 "SMT311_Q35A_SR_CAPTURE_S_PARM_STATE_ONLY fps=%u %ux%u unchanged=0\n",
			 fps, cam->fmt.width, cam->fmt.height);
	} else if (cam->powered && cam->initialized && !cam->streaming) {
		if (fps == 25) ret = t311_sr130pc20_apply(cam, sr130pc20_fps_25fix, ARRAY_SIZE(sr130pc20_fps_25fix), "fps25");
		else if (fps == 15) ret = t311_sr130pc20_apply(cam, sr130pc20_fps_15fix, ARRAY_SIZE(sr130pc20_fps_15fix), "fps15");
		else ret = t311_sr130pc20_apply(cam, sr130pc20_fps_7fix, ARRAY_SIZE(sr130pc20_fps_7fix), "fps7");
	}
	if (!ret)
		cam->fps = fps;
	fi->interval.numerator = 1;
	fi->interval.denominator = cam->fps;
out:
	mutex_unlock(&cam->lock);
	return ret;
}

static int t311_sr130pc20_prepare_fast_reconfig(struct t311_sr130pc20 *cam)
{
	const struct t311_sr130pc20_mode *mode;
	int ret;

	mode = t311_sr130pc20_find_mode(cam->fast_target_w, cam->fast_target_h);
	if (mode->width != cam->fast_target_w || mode->height != cam->fast_target_h)
		return -EINVAL;

	/* Q35O: do not program the target mode while the old CSIS/FIMC
	 * configuration is still active. Put SR130PC20 to sleep only, keep
	 * rails/MCLK alive, and defer the exact lt01 setfiles until sensor
	 * s_stream(1). FIMC starts CSIS before the sensor callback, so the
	 * receiver is already armed when Capture_Size -> Capture_Mode wakes
	 * the sensor. This exact combination was not tested by Q35L/Q35M. */
	ret = t311_sr130pc20_write_table(cam, t311_sr130pc20_stop_regs,
		ARRAY_SIZE(t311_sr130pc20_stop_regs), "q35o-fast-defer-stop");
	if (ret)
		return ret;

	cam->fast_reconfig_pending = false;
	cam->fast_reconfig_armed = true;
	cam->streaming = false;
	dev_info(cam->dev,
		 "SMT311_Q35O_FAST_RECONFIG_DEFER target=%ux%u mode=%s sensor_sleep=1 power_kept=1 apply_after_csis=1\n",
		 mode->width, mode->height, mode->capture ? "STILL" : "PREVIEW");
	return 0;
}

static int t311_sr130pc20_capture_timing(struct t311_sr130pc20 *cam)
{
	u8 frame_mode, sleep;
	int ret;
	size_t count = ARRAY_SIZE(sr130pc20_fps_auto);

	if (count < 4 || sr130pc20_fps_auto[count - 2] != 0x0300 ||
		sr130pc20_fps_auto[count - 1] != 0x0100)
		return -EINVAL;

	ret = t311_sr130pc20_write_table(cam, sr130pc20_fps_auto, count - 2,
		"q35q-capture-auto-timing-held-sleep");
	if (ret)
		return ret;
	ret = t311_sr130pc20_write8(cam, 0x03, 0x00);
	if (ret)
		return ret;
	ret = t311_sr130pc20_read8(cam, 0x11, &frame_mode);
	if (ret)
		return ret;
	ret = t311_sr130pc20_read8(cam, 0x01, &sleep);
	if (ret)
		return ret;
	if ((frame_mode & 0x04) || !(sleep & 0x01))
		return -EIO;
	dev_info(cam->dev,
		 "SMT311_Q35Q_CAPTURE_AUTO_TIMING frame_mode=%02x fixed_fps=0 sleep=%02x held_asleep=1\n",
		 frame_mode, sleep);
	return 0;
}

static int t311_sr130pc20_apply_fast_reconfig(struct t311_sr130pc20 *cam,
		const struct t311_sr130pc20_mode *mode)
{
	int ret;

	if (mode->capture) {
		ret = t311_sr130pc20_capture_timing(cam);
		if (ret)
			return ret;
		/* Exact non-KONA lt01/T311 stock sequence, intentionally kept as
		 * TWO tables. Capture_Size ends with wake; Capture_Mode immediately
		 * sleeps/reprograms/wakes again. Do not collapse the intermediate
		 * transition into the other-machine combined table. */
		ret = t311_sr130pc20_write_table(cam, mode->regs, mode->count,
			"q35o-capture-size-stock-lt01");
		if (ret)
			return ret;
		dev_info(cam->dev,
			 "SMT311_Q35O_CAPTURE_STAGE1_PASS target=%ux%u intermediate_wake=1\n",
			 mode->width, mode->height);
		ret = t311_sr130pc20_write_table(cam, SR130PC20_Capture_Mode,
			ARRAY_SIZE(SR130PC20_Capture_Mode), "q35o-capture-mode-stock-lt01");
		if (ret)
			return ret;
		dev_info(cam->dev,
			 "SMT311_Q35O_CAPTURE_STAGE2_PASS target=%ux%u final_wake=1\n",
			 mode->width, mode->height);
		t311_sr130pc20_q35o_capture_readback(cam);
	} else {
		if (!cam->fps)
			cam->fps = 25;
		if (cam->fps == 25)
			ret = t311_sr130pc20_apply(cam, sr130pc20_fps_25fix,
				ARRAY_SIZE(sr130pc20_fps_25fix), "q35p-preview-fps25");
		else if (cam->fps == 15)
			ret = t311_sr130pc20_apply(cam, sr130pc20_fps_15fix,
				ARRAY_SIZE(sr130pc20_fps_15fix), "q35p-preview-fps15");
		else
			ret = t311_sr130pc20_apply(cam, sr130pc20_fps_7fix,
				ARRAY_SIZE(sr130pc20_fps_7fix), "q35p-preview-fps7");
		if (ret)
			return ret;
		ret = t311_sr130pc20_write_table(cam, mode->regs, mode->count,
			"q35o-preview-size");
		if (ret)
			return ret;
		ret = t311_sr130pc20_write_table(cam, SR130PC20_Preview_Mode,
			ARRAY_SIZE(SR130PC20_Preview_Mode), "q35o-preview-mode");
		if (ret)
			return ret;
	}

	cam->fast_reconfig_armed = false;
	cam->fast_target_w = cam->fast_target_h = 0;
	cam->streaming = true;
	dev_info(cam->dev,
		 "SMT311_Q35O_FAST_RECONFIG_APPLIED_AFTER_CSIS target=%ux%u mode=%s split_stock_tables=%d\n",
		 mode->width, mode->height, mode->capture ? "STILL" : "PREVIEW",
		 mode->capture ? 1 : 0);
	return 0;
}

static int t311_sr130pc20_s_power(struct v4l2_subdev *sd, int on)
{
	struct t311_sr130pc20 *cam = to_t311_sr130pc20(sd);

	/*
	 * HOTFIX11: FIMC calls s_power(1) from video-node open().
	 * Android's external provider performs metadata-only opens, so do not
	 * physically cycle the front sensor there. Real hardware power belongs
	 * to STREAMON, symmetric with the rear HOTFIX9 lifecycle.
	 */
	mutex_lock(&cam->lock);
	if (!on) {
		cam->fast_reconfig_pending = false;
		cam->fast_reconfig_armed = false;
		cam->fast_target_w = cam->fast_target_h = 0;
		t311_sr130pc20_power_off(cam);
		dev_info(cam->dev, "SMT311_Q34P_SR_POWER_OFF close=1\n");
	}
	mutex_unlock(&cam->lock);

	return 0;
}

static int t311_sr130pc20_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct t311_sr130pc20 *cam = to_t311_sr130pc20(sd);
	const struct t311_sr130pc20_mode *mode;
	bool warm;
	int ret = 0;

	mutex_lock(&cam->lock);

	if (!enable) {
		if (!cam->powered || !cam->streaming)
			goto out;

		if (cam->fast_reconfig_pending) {
			ret = t311_sr130pc20_prepare_fast_reconfig(cam);
			if (ret) {
				dev_warn(cam->dev, "SMT311_Q35B_FAST_RECONFIG_PREPARE_FAIL ret=%d\n", ret);
				goto fail_reset;
			}
			goto out;
		}

		ret = t311_sr130pc20_write_table(cam,
			t311_sr130pc20_stop_regs,
			ARRAY_SIZE(t311_sr130pc20_stop_regs), "stream-off");
		if (!ret) {
			cam->streaming = false;
			dev_info(cam->dev,
				 "SMT311_Q34P_SR_WARM_STREAMOFF powered=1 initialized=%d\n",
				 cam->initialized);
		}
		goto out;
	}

	if (cam->fast_reconfig_armed) {
		if (cam->fmt.width == cam->fast_target_w &&
			cam->fmt.height == cam->fast_target_h) {
			mode = t311_sr130pc20_find_mode(cam->fmt.width, cam->fmt.height);
			dev_info(cam->dev,
				 "SMT311_Q35O_FAST_RECONFIG_STREAMON_ENTER target=%ux%u csis_should_be_running=1\n",
				 cam->fmt.width, cam->fmt.height);
			ret = t311_sr130pc20_apply_fast_reconfig(cam, mode);
			if (ret)
				goto fail_reset;
			goto out;
		}
		dev_warn(cam->dev,
			 "SMT311_Q35O_FAST_RECONFIG_MISMATCH armed=%ux%u active=%ux%u; falling_back_normal_streamon=1\n",
			 cam->fast_target_w, cam->fast_target_h, cam->fmt.width, cam->fmt.height);
		cam->fast_reconfig_armed = false;
		cam->fast_target_w = cam->fast_target_h = 0;
		cam->streaming = false;
	}

	if (cam->streaming)
		goto out;

	warm = cam->powered && cam->initialized;
	ret = t311_sr130pc20_power_on(cam);
	if (ret)
		goto out;

	if (!cam->initialized) {
		ret = t311_sr130pc20_detect(cam);
		if (ret)
			goto fail_reset;

		ret = t311_sr130pc20_write_table(cam,
			t311_sr130pc20_init_regs,
			ARRAY_SIZE(t311_sr130pc20_init_regs), "init");
		if (ret)
			goto fail_reset;
		cam->initialized = true;
		ret = __v4l2_ctrl_handler_setup(&cam->ctrls);
		if (ret)
			goto fail_reset;
		dev_info(cam->dev,
			 "PJARCZAK CAMERA H27: SR130PC20 INIT + controls PASS writes=%zu\n",
			 ARRAY_SIZE(t311_sr130pc20_init_regs));
	}

	mode = t311_sr130pc20_find_mode(cam->fmt.width, cam->fmt.height);

	if (mode->capture) {
		ret = t311_sr130pc20_capture_timing(cam);
		if (ret)
			goto fail_reset;
		/* Exact lt01/T311 Samsung path: Capture_Size first, then Capture_Mode.
		 * The combined *_Capture_Mode tables belong to other machine variants
		 * and omit the lt01 capture-size page-18 programming. */
		ret = t311_sr130pc20_write_table(cam, mode->regs, mode->count,
			"capture-size-stock-lt01");
		if (ret)
			goto fail_reset;
		ret = t311_sr130pc20_write_table(cam, SR130PC20_Capture_Mode,
			ARRAY_SIZE(SR130PC20_Capture_Mode), "capture-mode-stock-lt01");
		if (ret)
			goto fail_reset;
		dev_info(cam->dev,
			 "SMT311_Q35M_LT01_STOCK_CAPTURE_SETFILES %ux%u fps_state=%u capture_size_then_mode=1 no_fixed_fps=1\n",
			 cam->fmt.width, cam->fmt.height, cam->fps);
	} else {
		/* Stock s_parm() programs preview FPS, then start_preview() applies
		 * preview size and Preview_Mode. Preview_Mode remains last and wakes
		 * the sensor after fixed-FPS tables leave it asleep. */
		if (!cam->fps)
			cam->fps = 25;
		if (cam->fps == 25)
			ret = t311_sr130pc20_apply(cam, sr130pc20_fps_25fix,
				ARRAY_SIZE(sr130pc20_fps_25fix), "fps25-before-preview");
		else if (cam->fps == 15)
			ret = t311_sr130pc20_apply(cam, sr130pc20_fps_15fix,
				ARRAY_SIZE(sr130pc20_fps_15fix), "fps15-before-preview");
		else
			ret = t311_sr130pc20_apply(cam, sr130pc20_fps_7fix,
				ARRAY_SIZE(sr130pc20_fps_7fix), "fps7-before-preview");
		if (ret)
			goto fail_reset;
		dev_info(cam->dev, "SMT311_Q35A_SR_PREVIEW_FPS_BEFORE_MODE fps=%u %ux%u\n",
			 cam->fps, cam->fmt.width, cam->fmt.height);

		ret = t311_sr130pc20_write_table(cam, mode->regs, mode->count,
			"preview-size");
		if (ret)
			goto fail_reset;
		ret = t311_sr130pc20_write_table(cam, SR130PC20_Preview_Mode,
			ARRAY_SIZE(SR130PC20_Preview_Mode), "preview-mode");
		if (ret)
			goto fail_reset;
	}

	cam->streaming = true;
	dev_info(cam->dev,
		 "SMT311_Q34P_SR_%s_STREAMON %ux%u mode=%s fps=%u\n",
		 warm ? "WARM" : "COLD", cam->fmt.width, cam->fmt.height,
		 mode->capture ? "STILL" : "PREVIEW", cam->fps);
	dev_info(cam->dev,
		 "PJARCZAK CAMERA H27: SR130PC20 STREAM ON %ux%u UYVY MIPI1\n",
		 cam->fmt.width, cam->fmt.height);
	goto out;

fail_reset:
	dev_warn(cam->dev, "SMT311_Q34P_SR_STREAMON_FAIL ret=%d; cold reset\n", ret);
	t311_sr130pc20_power_off(cam);
out:
	mutex_unlock(&cam->lock);
	return ret;
}

static int t311_sr130pc20_enum_mbus_code(struct v4l2_subdev *sd,
					 struct v4l2_subdev_state *state,
					 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_UYVY8_2X8;
	return 0;
}

static int t311_sr130pc20_enum_frame_size(struct v4l2_subdev *sd,
					  struct v4l2_subdev_state *state,
					  struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->pad || fse->code != MEDIA_BUS_FMT_UYVY8_2X8 || fse->index >= ARRAY_SIZE(t311_sr130pc20_modes))
		return -EINVAL;
	fse->min_width = fse->max_width = t311_sr130pc20_modes[fse->index].width;
	fse->min_height = fse->max_height = t311_sr130pc20_modes[fse->index].height;
	return 0;
}

static int t311_sr130pc20_get_fmt(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_format *format)
{
	struct t311_sr130pc20 *cam = to_t311_sr130pc20(sd);

	if (format->pad)
		return -EINVAL;
	mutex_lock(&cam->lock);
	format->format = cam->fmt;
	mutex_unlock(&cam->lock);
	return 0;
}

static int t311_sr130pc20_set_fmt(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_format *format)
{
	struct t311_sr130pc20 *cam = to_t311_sr130pc20(sd);

	if (format->pad)
		return -EINVAL;

	{ const struct t311_sr130pc20_mode *mode=t311_sr130pc20_find_mode(format->format.width,format->format.height);
	format->format.width = mode->width;
	format->format.height = mode->height; }
	format->format.code = MEDIA_BUS_FMT_UYVY8_2X8;
	format->format.field = V4L2_FIELD_NONE;
	format->format.colorspace = V4L2_COLORSPACE_SRGB;
	format->format.ycbcr_enc = V4L2_YCBCR_ENC_601;
	format->format.quantization = V4L2_QUANTIZATION_LIM_RANGE;
	format->format.xfer_func = V4L2_XFER_FUNC_SRGB;

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		mutex_lock(&cam->lock);
		cam->fmt = format->format;
		mutex_unlock(&cam->lock);
	}
	return 0;
}

static const struct v4l2_subdev_core_ops t311_sr130pc20_core_ops = {
	.s_power = t311_sr130pc20_s_power,
};

static const struct v4l2_subdev_video_ops t311_sr130pc20_video_ops = {
	.s_stream = t311_sr130pc20_s_stream,
};

static const struct v4l2_subdev_pad_ops t311_sr130pc20_pad_ops = {
	.enum_mbus_code = t311_sr130pc20_enum_mbus_code,
	.enum_frame_size = t311_sr130pc20_enum_frame_size,
	.get_fmt = t311_sr130pc20_get_fmt,
	.set_fmt = t311_sr130pc20_set_fmt,
	.get_frame_interval = t311_sr130pc20_get_frame_interval,
	.set_frame_interval = t311_sr130pc20_set_frame_interval,
};

static const struct v4l2_subdev_ops t311_sr130pc20_ops = {
	.core = &t311_sr130pc20_core_ops,
	.video = &t311_sr130pc20_video_ops,
	.pad = &t311_sr130pc20_pad_ops,
};

static int t311_sr130pc20_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct t311_sr130pc20 *cam;
	int ret;

	cam = devm_kzalloc(dev, sizeof(*cam), GFP_KERNEL);
	if (!cam)
		return -ENOMEM;
	cam->dev = dev;
	mutex_init(&cam->lock);

	cam->core = devm_regulator_get(dev, "core");
	if (IS_ERR(cam->core))
		return dev_err_probe(dev, PTR_ERR(cam->core), "missing core supply\n");
	cam->io = devm_regulator_get(dev, "io");
	if (IS_ERR(cam->io))
		return dev_err_probe(dev, PTR_ERR(cam->io), "missing io supply\n");
	cam->analog = devm_regulator_get(dev, "analog");
	if (IS_ERR(cam->analog))
		return dev_err_probe(dev, PTR_ERR(cam->analog), "missing analog supply\n");
	cam->vt = devm_regulator_get(dev, "vt");
	if (IS_ERR(cam->vt))
		return dev_err_probe(dev, PTR_ERR(cam->vt), "missing vt supply\n");
	cam->xclk = devm_clk_get(dev, "xclk");
	if (IS_ERR(cam->xclk))
		return dev_err_probe(dev, PTR_ERR(cam->xclk), "missing xclk\n");
	cam->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(cam->pinctrl))
		return dev_err_probe(dev, PTR_ERR(cam->pinctrl), "missing sensor pinctrl\n");
	cam->pins_mclk_active = pinctrl_lookup_state(cam->pinctrl, "mclk-active");
	if (IS_ERR(cam->pins_mclk_active))
		return dev_err_probe(dev, PTR_ERR(cam->pins_mclk_active), "missing mclk-active pinctrl state\n");
	cam->pins_mclk_idle = pinctrl_lookup_state(cam->pinctrl, "mclk-idle");
	if (IS_ERR(cam->pins_mclk_idle))
		return dev_err_probe(dev, PTR_ERR(cam->pins_mclk_idle), "missing mclk-idle pinctrl state\n");
	ret = pinctrl_select_state(cam->pinctrl, cam->pins_mclk_idle);
	if (ret)
		return dev_err_probe(dev, ret, "failed selecting initial MCLK idle state\n");

	cam->standby = devm_gpiod_get(dev, "standby",
			GPIOD_OUT_HIGH | GPIOD_FLAGS_BIT_NONEXCLUSIVE);
	if (IS_ERR(cam->standby))
		return dev_err_probe(dev, PTR_ERR(cam->standby), "missing standby gpio\n");
	cam->reset = devm_gpiod_get(dev, "reset",
			GPIOD_OUT_HIGH | GPIOD_FLAGS_BIT_NONEXCLUSIVE);
	if (IS_ERR(cam->reset))
		return dev_err_probe(dev, PTR_ERR(cam->reset), "missing reset gpio\n");

	v4l2_i2c_subdev_init(&cam->sd, client, &t311_sr130pc20_ops);
	ret=t311_sr130pc20_init_controls(cam);
	if(ret) goto err_mutex;
	cam->fps=0;
	cam->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	cam->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	cam->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&cam->sd.entity, 1, &cam->pad);
	if (ret)
		goto err_mutex;

	/*
	 * Linux 6.12 pad-op wrappers require subdev state for TRY formats.
	 * exynos4-is performs a TRY negotiation during capture open, so finalize
	 * the subdev state before async registration. Without this, the V4L2
	 * core rejects set_fmt() with -EINVAL before our callback is entered.
	 */
	ret = v4l2_subdev_init_finalize(&cam->sd);
	if (ret)
		goto err_entity;
	dev_info(cam->dev,
		 "PJARCZAK CAMERA V210: SR130PC20 subdev state finalized\n");

	cam->fmt.width = 640;
	cam->fmt.height = 480;
	cam->fmt.code = MEDIA_BUS_FMT_UYVY8_2X8;
	cam->fmt.field = V4L2_FIELD_NONE;
	cam->fmt.colorspace = V4L2_COLORSPACE_SRGB;
	cam->fmt.ycbcr_enc = V4L2_YCBCR_ENC_601;
	cam->fmt.quantization = V4L2_QUANTIZATION_LIM_RANGE;
	cam->fmt.xfer_func = V4L2_XFER_FUNC_SRGB;

	ret = t311_sr130pc20_power_on(cam);
	if (ret)
		goto err_subdev;
	ret = t311_sr130pc20_detect(cam);
	t311_sr130pc20_power_off(cam);
	if (ret)
		goto err_subdev;

	ret = v4l2_async_register_subdev_sensor(&cam->sd);
	if (ret)
		goto err_subdev;
	return 0;

err_subdev:
	v4l2_subdev_cleanup(&cam->sd);
err_entity:
	media_entity_cleanup(&cam->sd.entity);
err_mutex:
	v4l2_ctrl_handler_free(&cam->ctrls);
	mutex_destroy(&cam->lock);
	return ret;
}

static void t311_sr130pc20_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct t311_sr130pc20 *cam = to_t311_sr130pc20(sd);

	v4l2_async_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&cam->ctrls);
	v4l2_subdev_cleanup(sd);
	mutex_lock(&cam->lock);
	t311_sr130pc20_power_off(cam);
	mutex_unlock(&cam->lock);
	media_entity_cleanup(&sd->entity);
	mutex_destroy(&cam->lock);
}

static const struct of_device_id t311_sr130pc20_of_match[] = {
	{ .compatible = "siliconfile,sr130pc20-t311" },
	{ }
};
MODULE_DEVICE_TABLE(of, t311_sr130pc20_of_match);

static const struct i2c_device_id t311_sr130pc20_id[] = {
	{ "sr130pc20-t311" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, t311_sr130pc20_id);

static struct i2c_driver t311_sr130pc20_driver = {
	.driver = {
		.name = "t311-sr130pc20",
		.of_match_table = t311_sr130pc20_of_match,
	},
	.probe = t311_sr130pc20_probe,
	.remove = t311_sr130pc20_remove,
	.id_table = t311_sr130pc20_id,
};
module_i2c_driver(t311_sr130pc20_driver);

MODULE_DESCRIPTION("SM-T311 SR130PC20 Linux 6.12 VGA streaming driver");
MODULE_AUTHOR("PJARCZAK bring-up");
MODULE_LICENSE("GPL");
