// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sony ISX012 driver for Samsung Galaxy Tab 3 8.0 SM-T311.
 *
 * v214: modern V4L2 subdev, exact LT01 register set, burst I2C,
 * OTP shading calibration and non-blocking absent-sensor registration.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/unaligned.h>

#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-async.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>

#define T311_ISX012_CHIP_ID_REG 0x0000
#define T311_ISX012_CHIP_ID 0x6017
#define T311_ISX012_XCLK 12000000UL
#define T311_ISX012_BURST_MAX 1200
#define T311_ISX012_REG_INTSTS 0x000e
#define T311_ISX012_REG_INTCLR 0x0012
#define T311_ISX012_INT_OM BIT(0)
#define T311_ISX012_INT_CM BIT(1)
#define T311_ISX012_REG_HSIZE_MONI 0x0090
#define T311_ISX012_REG_VSIZE_MONI 0x0096
#define T311_ISX012_REG_HSIZE_CAP 0x0092
#define T311_ISX012_REG_VSIZE_CAP 0x0098
#define T311_ISX012_REG_AF_STATE 0x8b8a
#define T311_ISX012_REG_AF_RESULT 0x8b8b
#define V4L2_CID_T311_AF_ROI_X (V4L2_CID_USER_BASE + 0x1100)
#define V4L2_CID_T311_AF_ROI_Y (V4L2_CID_USER_BASE + 0x1101)

struct t311_isx012_reg {
	u16 subaddr;
	u32 value;
	u8 len;
};
typedef struct t311_isx012_reg isx012_regset_t;
#include "t311-isx012-regs-lt01.h"

struct t311_isx012_mode {
	u16 width;
	u16 height;
	bool capture;
};

static const struct t311_isx012_mode t311_isx012_modes[] = {
	/* Stock LT01 monitoring modes. */
	{ 176, 144, false }, { 320, 240, false }, { 352, 288, false },
	{ 528, 432, false }, { 640, 480, false }, { 720, 480, false },
	{ 880, 720, false }, { 1024, 576, false }, { 1024, 768, false },
	{ 1280, 720, false },
	/* Stock still-capture modes; H27 runtime mode-sweep qualifies each one. */
	{ 960, 720, true }, { 1536, 864, true }, { 1600, 1200, true },
	{ 2048, 1152, true }, { 2048, 1536, true }, { 2560, 1440, true },
	{ 2560, 1920, true },
};

struct t311_isx012 {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct mutex lock;
	struct device *dev;
	struct regulator *core;
	struct regulator *io;
	struct regulator *analog;
	struct regulator *vt;
	struct regulator *af;
	struct clk *xclk;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_mclk_active;
	struct pinctrl_state *pins_mclk_idle;
	struct gpio_desc *reset;
	struct gpio_desc *standby;
	struct gpio_desc *vt_standby;
	struct gpio_desc *vt_reset;
	struct v4l2_mbus_framefmt fmt;
	struct v4l2_ctrl_handler ctrls;
	struct v4l2_ctrl *iso_auto;
	struct v4l2_ctrl *iso;
	struct v4l2_ctrl *af_status;
	struct v4l2_ctrl *af_roi_x;
	struct v4l2_ctrl *af_roi_y;
	u32 fps;
	bool powered;
	bool initialized;
	bool present;
	bool streaming;
};

static inline struct t311_isx012 *to_t311_isx012(struct v4l2_subdev *sd)
{
	return container_of(sd, struct t311_isx012, sd);
}

static int t311_isx012_xfer(struct t311_isx012 *cam, const u8 *buf, size_t len)
{
	struct i2c_client *client = v4l2_get_subdevdata(&cam->sd);
	struct i2c_msg msg = { .addr = client->addr, .flags = 0, .len = len, .buf = (u8 *)buf };
	int retry, ret = -EIO;

	for (retry = 0; retry < 5; retry++) {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret == 1)
			return 0;
		usleep_range(1000, 1500);
	}
	return ret < 0 ? ret : -EIO;
}

static int t311_isx012_write(struct t311_isx012 *cam, u16 reg, u32 value, u8 len)
{
	u8 buf[6] = { reg >> 8, reg & 0xff };

	if (len == 1) {
		buf[2] = value;
	} else if (len == 2) {
		put_unaligned_le16((u16)value, &buf[2]);
	} else {
		return -EINVAL;
	}
	return t311_isx012_xfer(cam, buf, len + 2);
}

static int t311_isx012_read(struct t311_isx012 *cam, u16 reg, u32 *value, u8 len)
{
	struct i2c_client *client = v4l2_get_subdevdata(&cam->sd);
	u8 addr[2] = { reg >> 8, reg & 0xff };
	u8 data[2] = {};
	struct i2c_msg msgs[] = {
		{ .addr = client->addr, .flags = 0, .len = 2, .buf = addr },
		{ .addr = client->addr, .flags = I2C_M_RD, .len = len, .buf = data },
	};
	int retry, ret = -EIO;

	if (len != 1 && len != 2)
		return -EINVAL;
	for (retry = 0; retry < 5; retry++) {
		ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
		if (ret == ARRAY_SIZE(msgs)) {
			*value = len == 1 ? data[0] : get_unaligned_le16(data);
			return 0;
		}
		usleep_range(1000, 1500);
	}
	return ret < 0 ? ret : -EIO;
}

static int t311_isx012_write_table_burst(struct t311_isx012 *cam,
					 const isx012_regset_t *regs, size_t count)
{
	u8 *buf;
	size_t i = 0;
	int ret = 0;

	buf = kmalloc(T311_ISX012_BURST_MAX, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	while (i < count) {
		size_t pos = 2, j = i;
		u32 next;

		if (regs[i].subaddr == 0xffff) {
			msleep(regs[i].value);
			i++;
			continue;
		}

		buf[0] = regs[i].subaddr >> 8;
		buf[1] = regs[i].subaddr & 0xff;
		next = regs[i].subaddr;

		while (j < count && regs[j].subaddr != 0xffff &&
		       regs[j].subaddr == next &&
		       pos + regs[j].len <= T311_ISX012_BURST_MAX) {
			if (regs[j].len == 1) {
				buf[pos++] = regs[j].value;
			} else if (regs[j].len == 2) {
				put_unaligned_le16((u16)regs[j].value, &buf[pos]);
				pos += 2;
			} else {
				ret = -EINVAL;
				goto out;
			}
			next += regs[j].len;
			j++;
		}

		ret = t311_isx012_xfer(cam, buf, pos);
		if (ret)
			goto out;
		i = j;
	}
out:
	kfree(buf);
	return ret;
}

static int t311_isx012_write_table_seq(struct t311_isx012 *cam,
				       const isx012_regset_t *regs, size_t count)
{
	size_t i;
	int ret;

	for (i = 0; i < count; i++) {
		if (regs[i].subaddr == 0xffff) {
			msleep(regs[i].value);
			continue;
		}
		ret = t311_isx012_write(cam, regs[i].subaddr, regs[i].value,
					 regs[i].len);
		if (ret)
			return ret;
	}
	return 0;
}

#define T311_WRITE_TABLE_BURST(cam, table) \
	t311_isx012_write_table_burst((cam), (table), ARRAY_SIZE(table))
#define T311_WRITE_TABLE_SEQ(cam, table) \
	t311_isx012_write_table_seq((cam), (table), ARRAY_SIZE(table))

static int t311_isx012_wait_irq(struct t311_isx012 *cam, u8 bit, unsigned int loops)
{
	u32 value;
	unsigned int i;
	int ret;

	for (i = 0; i < loops; i++) {
		ret = t311_isx012_read(cam, T311_ISX012_REG_INTSTS, &value, 1);
		if (ret)
			return ret;
		if (value & bit)
			break;
		msleep(5);
	}
	if (i == loops)
		return -ETIMEDOUT;

	for (i = 0; i < loops; i++) {
		ret = t311_isx012_write(cam, T311_ISX012_REG_INTCLR, bit, 1);
		if (ret)
			return ret;
		ret = t311_isx012_read(cam, T311_ISX012_REG_INTSTS, &value, 1);
		if (ret)
			return ret;
		if (!(value & bit))
			return 0;
		msleep(5);
	}
	return -ETIMEDOUT;
}

static int t311_isx012_power_on(struct t311_isx012 *cam)
{
	int ret;

	if (cam->powered)
		return 0;

	dev_info(cam->dev,
		 "PJARCZAK CAMERA HOTFIX11: rear POWER PRE core=%d io=%d analog=%d vt=%d af=%d xclk=%lu\n",
		 regulator_is_enabled(cam->core), regulator_is_enabled(cam->io),
		 regulator_is_enabled(cam->analog), regulator_is_enabled(cam->vt),
		 regulator_is_enabled(cam->af), clk_get_rate(cam->xclk));

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

	gpiod_set_value_cansleep(cam->vt_standby, 0);
	udelay(20);
	ret = pinctrl_select_state(cam->pinctrl, cam->pins_mclk_active);
	if (ret)
		goto err_vt;
	dev_dbg(cam->dev, "PJARCZAK CAMERA HOTFIX10: rear MCLK pin ACTIVE\n");
	ret = clk_set_rate(cam->xclk, T311_ISX012_XCLK);
	if (ret)
		goto err_vt;
	ret = clk_prepare_enable(cam->xclk);
	if (ret)
		goto err_vt;
	usleep_range(11000, 12000);
	gpiod_set_value_cansleep(cam->vt_reset, 0);
	usleep_range(1100, 1200);
	gpiod_set_value_cansleep(cam->vt_standby, 1);
	udelay(20);
	gpiod_set_value_cansleep(cam->reset, 0);
	udelay(10);
	ret = regulator_enable(cam->af);
	if (ret)
		goto err_clk;
	usleep_range(6000, 6500);
	/* Stock LT01 sensor driver waits again after board power sequencing. */
	msleep(10);
	cam->powered = true;
	dev_info(cam->dev,
		 "PJARCZAK CAMERA HOTFIX11: rear POWER ON core=%d io=%d analog=%d vt=%d af=%d xclk=%lu reset=%d standby=%d vt_reset=%d vt_standby=%d\n",
		 regulator_is_enabled(cam->core), regulator_is_enabled(cam->io),
		 regulator_is_enabled(cam->analog), regulator_is_enabled(cam->vt),
		 regulator_is_enabled(cam->af), clk_get_rate(cam->xclk),
		 gpiod_get_value_cansleep(cam->reset), gpiod_get_value_cansleep(cam->standby),
		 gpiod_get_value_cansleep(cam->vt_reset), gpiod_get_value_cansleep(cam->vt_standby));
	return 0;

err_clk:
	clk_disable_unprepare(cam->xclk);
err_vt:
	pinctrl_select_state(cam->pinctrl, cam->pins_mclk_idle);
	gpiod_set_value_cansleep(cam->reset, 1);
	gpiod_set_value_cansleep(cam->vt_reset, 1);
	gpiod_set_value_cansleep(cam->vt_standby, 1);
	regulator_disable(cam->vt);
err_analog:
	regulator_disable(cam->analog);
err_io:
	regulator_disable(cam->io);
err_core:
	regulator_disable(cam->core);
	return ret;
}

static void t311_isx012_power_off(struct t311_isx012 *cam)
{
	if (!cam->powered)
		return;

	regulator_disable(cam->af);
	udelay(10);
	gpiod_set_value_cansleep(cam->vt_reset, 1);
	udelay(10);
	gpiod_set_value_cansleep(cam->standby, 1);
	udelay(10);
	gpiod_set_value_cansleep(cam->reset, 1);
	udelay(50);
	clk_disable_unprepare(cam->xclk);
	pinctrl_select_state(cam->pinctrl, cam->pins_mclk_idle);
	dev_dbg(cam->dev, "PJARCZAK CAMERA HOTFIX10: rear MCLK pin IDLE\n");
	udelay(10);
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
		 "PJARCZAK CAMERA HOTFIX11: rear POWER OFF core=%d io=%d analog=%d vt=%d af=%d xclk=%lu reset=%d standby=%d vt_reset=%d vt_standby=%d\n",
		 regulator_is_enabled(cam->core), regulator_is_enabled(cam->io),
		 regulator_is_enabled(cam->analog), regulator_is_enabled(cam->vt),
		 regulator_is_enabled(cam->af), clk_get_rate(cam->xclk),
		 gpiod_get_value_cansleep(cam->reset), gpiod_get_value_cansleep(cam->standby),
		 gpiod_get_value_cansleep(cam->vt_reset), gpiod_get_value_cansleep(cam->vt_standby));
}

static int t311_isx012_detect(struct t311_isx012 *cam)
{
	u32 chip;
	int ret = t311_isx012_read(cam, T311_ISX012_CHIP_ID_REG, &chip, 2);

	if (ret) {
		dev_err(cam->dev, "PJARCZAK CAMERA HOTFIX5: ISX012 I2C identity read failed ret=%d\n", ret);
		return ret;
	}

	/*
	 * Stock LT01 only used 0x0000 as an optional diagnostic read.  It did
	 * not reject the sensor when the value differed from 0x6017.  Treat a
	 * successful I2C transfer as the presence check and validate the sensor
	 * with the stock OTP/vendor and OM/CM boot sequence below.
	 */
	dev_info(cam->dev,
		 "PJARCZAK CAMERA HOTFIX5: ISX012 identity reg0=0x%04x expected=0x%04x informational-only\n",
		 chip, T311_ISX012_CHIP_ID);
	return 0;
}

static int t311_isx012_read_vendor_id(struct t311_isx012 *cam)
{
	u32 status, temp;
	u16 vendor_id;
	bool otp1;
	int ret;

	ret = t311_isx012_read(cam, 0x004f, &status, 2);
	if (ret)
		return ret;
	otp1 = !!(status & 0x10);
	ret = t311_isx012_read(cam, otp1 ? 0x0051 : 0x0042, &temp, 2);
	if (ret)
		return ret;
	temp &= 0xfffc;
	vendor_id = ((temp & 0x03fc) << 6) | ((temp & 0xfc00) >> 10);
	dev_info(cam->dev,
		 "PJARCZAK CAMERA HOTFIX5: ISX012 OTP%d status=0x%04x vendor-id=0x%04x\n",
		 otp1 ? 1 : 0, status, vendor_id);
	return 0;
}

static int t311_isx012_apply_shading(struct t311_isx012 *cam)
{
	u32 status, v;
	int ret;
	bool otp1;

	ret = t311_isx012_read(cam, 0x004f, &status, 2);
	if (ret)
		return ret;
	otp1 = !!(status & 0x10);
	if (!otp1) {
		ret = t311_isx012_read(cam, 0x0040, &status, 2);
		if (ret)
			return ret;
		if (!(status & 0x10))
			return T311_WRITE_TABLE_BURST(cam, ISX012_Shading_Nocal);
	}

	ret = t311_isx012_read(cam, otp1 ? 0x005c : 0x004d, &status, 2);
	if (ret)
		return ret;
	switch ((status & 0x03c0) >> 6) {
	case 0: ret = T311_WRITE_TABLE_BURST(cam, ISX012_Shading_0); break;
	case 1: ret = T311_WRITE_TABLE_BURST(cam, ISX012_Shading_1); break;
	case 2: ret = T311_WRITE_TABLE_BURST(cam, ISX012_Shading_2); break;
	default: ret = T311_WRITE_TABLE_BURST(cam, ISX012_Shading_Nocal); break;
	}
	if (ret)
		return ret;

	ret = t311_isx012_read(cam, otp1 ? 0x0054 : 0x0045, &v, 2);
	if (ret) return ret;
	ret = t311_isx012_write(cam, 0x6804, v & 0x3fff, 2);
	if (ret) return ret;
	ret = t311_isx012_read(cam, otp1 ? 0x0056 : 0x0047, &v, 2);
	if (ret) return ret;
	ret = t311_isx012_write(cam, 0x6806, v & 0x3fff, 2);
	if (ret) return ret;
	ret = t311_isx012_read(cam, otp1 ? 0x005a : 0x004b, &v, 2);
	if (ret) return ret;
	ret = t311_isx012_write(cam, 0x6808, (v & 0x0ffc) >> 2, 2);
	if (ret) return ret;
	ret = t311_isx012_read(cam, otp1 ? 0x005b : 0x004c, &v, 2);
	if (ret) return ret;
	return t311_isx012_write(cam, 0x680a, (v & 0x3ff0) >> 4, 2);
}

static int t311_isx012_hw_init(struct t311_isx012 *cam)
{
	int ret;

	if (cam->initialized)
		return 0;
	ret = t311_isx012_detect(cam);
	if (ret)
		return ret;
	ret = t311_isx012_read_vendor_id(cam);
	if (ret) {
		dev_err(cam->dev, "PJARCZAK CAMERA HOTFIX5: ISX012 vendor-id read failed ret=%d\n", ret);
		return ret;
	}
	dev_info(cam->dev, "PJARCZAK CAMERA HOTFIX5: stage=identity+vendor PASS\n");

	ret = t311_isx012_wait_irq(cam, T311_ISX012_INT_OM, 30);
	if (ret)
		return ret;
	dev_info(cam->dev, "PJARCZAK CAMERA HOTFIX5: stage=OM0 PASS\n");
	ret = T311_WRITE_TABLE_BURST(cam, ISX012_Pll_Setting_4);
	if (ret)
		return ret;
	msleep(10);
	ret = t311_isx012_wait_irq(cam, T311_ISX012_INT_OM, 30);
	if (ret)
		return ret;

	ret = t311_isx012_write(cam, 0x00bf, 0x01, 1);
	if (ret)
		return ret;
	msleep(1);
	gpiod_set_value_cansleep(cam->standby, 0);
	msleep(50);
	ret = t311_isx012_wait_irq(cam, T311_ISX012_INT_OM, 30);
	if (ret)
		return ret;
	ret = t311_isx012_wait_irq(cam, T311_ISX012_INT_CM, 280);
	if (ret)
		return ret;
	dev_info(cam->dev, "PJARCZAK CAMERA HOTFIX5: stage=ACTIVE_CM PASS\n");
	ret = t311_isx012_write(cam, 0x5008, 0x00, 1);
	if (ret)
		return ret;
	ret = t311_isx012_apply_shading(cam);
	if (ret)
		return ret;
	ret = T311_WRITE_TABLE_BURST(cam, ISX012_Init_Reg);
	if (ret)
		return ret;
	ret = T311_WRITE_TABLE_SEQ(cam, isx012_fps_auto);
	if (ret)
		return ret;
	cam->initialized = true;
	/* Apply cached/default V4L2 controls after every cold power-up. */
	ret = __v4l2_ctrl_handler_setup(&cam->ctrls);
	if (ret) { cam->initialized = false; return ret; }
	dev_info(cam->dev, "PJARCZAK CAMERA H27: ISX012 LT01 init/calibration/controls complete\n");
	return 0;
}


static int t311_isx012_set_iso(struct t311_isx012 *cam, int iso)
{
	if (!cam->powered || !cam->initialized)
		return 0;
	if (cam->iso_auto && cam->iso_auto->val == V4L2_ISO_SENSITIVITY_AUTO)
		return T311_WRITE_TABLE_SEQ(cam, isx012_ISO_Auto);
	if (iso <= 50) return T311_WRITE_TABLE_SEQ(cam, isx012_ISO_50);
	if (iso <= 100) return T311_WRITE_TABLE_SEQ(cam, isx012_ISO_100);
	if (iso <= 200) return T311_WRITE_TABLE_SEQ(cam, isx012_ISO_200);
	return T311_WRITE_TABLE_SEQ(cam, isx012_ISO_400);
}

static int t311_isx012_apply_af_roi(struct t311_isx012 *cam)
{
	u32 x, y, w = 512, h = 512;
	int ret;
	if (!cam->powered || !cam->initialized || !cam->af_roi_x || !cam->af_roi_y)
		return 0;
	x = clamp_t(u32, cam->af_roi_x->val, 0, 2591);
	y = clamp_t(u32, cam->af_roi_y->val, 0, 1943);
	if (x > w / 2) x -= w / 2; else x = 0;
	if (y > h / 2) y -= h / 2; else y = 0;
	if (x + w > 2592) x = 2592 - w;
	if (y + h > 1944) y = 1944 - h;
	ret = t311_isx012_write(cam, 0x6a50, x, 2); if (ret) return ret;
	ret = t311_isx012_write(cam, 0x6a52, y, 2); if (ret) return ret;
	ret = t311_isx012_write(cam, 0x6a54, w, 2); if (ret) return ret;
	ret = t311_isx012_write(cam, 0x6a56, h, 2); if (ret) return ret;
	return T311_WRITE_TABLE_SEQ(cam, ISX012_AF_Window_Set);
}





static const s64 t311_isx012_iso_qmenu[] = { 50, 100, 200, 400 };

static int t311_isx012_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct t311_isx012 *cam = container_of(ctrl->handler, struct t311_isx012, ctrls);
	int ret = 0;

	if (!cam->powered || !cam->initialized)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		switch (ctrl->val) {
		case -4: ret = T311_WRITE_TABLE_SEQ(cam, ISX012_ExpSetting_M4Step); break;
		case -3: ret = T311_WRITE_TABLE_SEQ(cam, ISX012_ExpSetting_M3Step); break;
		case -2: ret = T311_WRITE_TABLE_SEQ(cam, ISX012_ExpSetting_M2Step); break;
		case -1: ret = T311_WRITE_TABLE_SEQ(cam, ISX012_ExpSetting_M1Step); break;
		case 1: ret = T311_WRITE_TABLE_SEQ(cam, ISX012_ExpSetting_P1Step); break;
		case 2: ret = T311_WRITE_TABLE_SEQ(cam, ISX012_ExpSetting_P2Step); break;
		case 3: ret = T311_WRITE_TABLE_SEQ(cam, ISX012_ExpSetting_P3Step); break;
		case 4: ret = T311_WRITE_TABLE_SEQ(cam, ISX012_ExpSetting_P4Step); break;
		default: ret = T311_WRITE_TABLE_SEQ(cam, ISX012_ExpSetting_Default); break;
		}
		break;
	case V4L2_CID_CONTRAST:
		if (ctrl->val <= -2) ret=T311_WRITE_TABLE_SEQ(cam,isx012_Contrast_Minus_2);
		else if (ctrl->val == -1) ret=T311_WRITE_TABLE_SEQ(cam,isx012_Contrast_Minus_1);
		else if (ctrl->val == 1) ret=T311_WRITE_TABLE_SEQ(cam,isx012_Contrast_Plus_1);
		else if (ctrl->val >= 2) ret=T311_WRITE_TABLE_SEQ(cam,isx012_Contrast_Plus_2);
		else ret=T311_WRITE_TABLE_SEQ(cam,isx012_Contrast_Default);
		break;
	case V4L2_CID_SATURATION:
		if (ctrl->val <= -2) ret=T311_WRITE_TABLE_SEQ(cam,isx012_Saturation_Minus_2);
		else if (ctrl->val == -1) ret=T311_WRITE_TABLE_SEQ(cam,isx012_Saturation_Minus_1);
		else if (ctrl->val == 1) ret=T311_WRITE_TABLE_SEQ(cam,isx012_Saturation_Plus_1);
		else if (ctrl->val >= 2) ret=T311_WRITE_TABLE_SEQ(cam,isx012_Saturation_Plus_2);
		else ret=T311_WRITE_TABLE_SEQ(cam,isx012_Saturation_Default);
		break;
	case V4L2_CID_SHARPNESS:
		if (ctrl->val <= -2) ret=T311_WRITE_TABLE_SEQ(cam,isx012_Sharpness_Minus_2);
		else if (ctrl->val == -1) ret=T311_WRITE_TABLE_SEQ(cam,isx012_Sharpness_Minus_1);
		else if (ctrl->val == 1) ret=T311_WRITE_TABLE_SEQ(cam,isx012_Sharpness_Plus_1);
		else if (ctrl->val >= 2) ret=T311_WRITE_TABLE_SEQ(cam,isx012_Sharpness_Plus_2);
		else ret=T311_WRITE_TABLE_SEQ(cam,isx012_Sharpness_Default);
		break;
	case V4L2_CID_EXPOSURE_METERING:
		if (ctrl->val == V4L2_EXPOSURE_METERING_CENTER_WEIGHTED)
			ret=T311_WRITE_TABLE_SEQ(cam,isx012_Metering_Center);
		else if (ctrl->val == V4L2_EXPOSURE_METERING_SPOT)
			ret=T311_WRITE_TABLE_SEQ(cam,isx012_Metering_Spot);
		else ret=T311_WRITE_TABLE_SEQ(cam,isx012_Metering_Matrix);
		break;
	case V4L2_CID_ISO_SENSITIVITY_AUTO:
	case V4L2_CID_ISO_SENSITIVITY:
		ret=t311_isx012_set_iso(cam, cam->iso ? t311_isx012_iso_qmenu[cam->iso->val] : 100);
		break;
	case V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE:
		switch(ctrl->val) {
		case V4L2_WHITE_BALANCE_DAYLIGHT: ret=T311_WRITE_TABLE_SEQ(cam,isx012_WB_Sunny); break;
		case V4L2_WHITE_BALANCE_CLOUDY: ret=T311_WRITE_TABLE_SEQ(cam,isx012_WB_Cloudy); break;
		case V4L2_WHITE_BALANCE_INCANDESCENT: ret=T311_WRITE_TABLE_SEQ(cam,isx012_WB_Tungsten); break;
		case V4L2_WHITE_BALANCE_FLUORESCENT: ret=T311_WRITE_TABLE_SEQ(cam,isx012_WB_Fluorescent); break;
		default: ret=T311_WRITE_TABLE_SEQ(cam,isx012_WB_Auto); break;
		} break;
	case V4L2_CID_COLORFX:
		switch(ctrl->val) {
		case V4L2_COLORFX_BW: ret=T311_WRITE_TABLE_SEQ(cam,isx012_Effect_Black_White); break;
		case V4L2_COLORFX_SEPIA: ret=T311_WRITE_TABLE_SEQ(cam,isx012_Effect_Sepia); break;
		case V4L2_COLORFX_NEGATIVE: ret=T311_WRITE_TABLE_SEQ(cam,ISX012_Effect_Negative); break;
		case V4L2_COLORFX_SKETCH: ret=T311_WRITE_TABLE_SEQ(cam,isx012_Effect_Sketch); break;
		case V4L2_COLORFX_SOLARIZATION: ret=T311_WRITE_TABLE_SEQ(cam,isx012_Effect_Solar); break;
		default: ret=T311_WRITE_TABLE_SEQ(cam,isx012_Effect_Normal); break;
		} break;
	case V4L2_CID_SCENE_MODE:
		switch(ctrl->val) {
		case V4L2_SCENE_MODE_BACKLIGHT: ret=T311_WRITE_TABLE_SEQ(cam,isx012_Scene_Backlight); break;
		case V4L2_SCENE_MODE_BEACH_SNOW: ret=T311_WRITE_TABLE_SEQ(cam,isx012_Scene_Beach_Snow); break;
		case V4L2_SCENE_MODE_CANDLE_LIGHT: ret=T311_WRITE_TABLE_SEQ(cam,isx012_Scene_Candle_Light); break;
		case V4L2_SCENE_MODE_DAWN_DUSK: ret=T311_WRITE_TABLE_SEQ(cam,isx012_Scene_Duskdawn); break;
		case V4L2_SCENE_MODE_FALL_COLORS: ret=T311_WRITE_TABLE_SEQ(cam,isx012_Scene_Fall_Color); break;
		case V4L2_SCENE_MODE_FIREWORKS: ret=T311_WRITE_TABLE_SEQ(cam,isx012_Scene_Fireworks); break;
		case V4L2_SCENE_MODE_LANDSCAPE: ret=T311_WRITE_TABLE_SEQ(cam,isx012_Scene_Landscape); break;
		case V4L2_SCENE_MODE_NIGHT: ret=T311_WRITE_TABLE_SEQ(cam,isx012_Scene_Nightshot); break;
		case V4L2_SCENE_MODE_PARTY_INDOOR: ret=T311_WRITE_TABLE_SEQ(cam,isx012_Scene_Party_Indoor); break;
		case V4L2_SCENE_MODE_PORTRAIT: ret=T311_WRITE_TABLE_SEQ(cam,isx012_Scene_Portrait); break;
		case V4L2_SCENE_MODE_SPORTS: ret=T311_WRITE_TABLE_SEQ(cam,isx012_Scene_Sports); break;
		case V4L2_SCENE_MODE_SUNSET: ret=T311_WRITE_TABLE_SEQ(cam,isx012_Scene_Sunset); break;
		case V4L2_SCENE_MODE_TEXT: ret=T311_WRITE_TABLE_SEQ(cam,isx012_Scene_Text); break;
		default: ret=T311_WRITE_TABLE_SEQ(cam,isx012_Scene_Default); break;
		} break;
	case V4L2_CID_3A_LOCK:
		ret = (ctrl->val & V4L2_LOCK_EXPOSURE) ? T311_WRITE_TABLE_SEQ(cam,isx012_ae_lock) : T311_WRITE_TABLE_SEQ(cam,isx012_ae_unlock);
		if (!ret) ret = (ctrl->val & V4L2_LOCK_WHITE_BALANCE) ? T311_WRITE_TABLE_SEQ(cam,isx012_awb_lock) : T311_WRITE_TABLE_SEQ(cam,isx012_awb_unlock);
		break;
	case V4L2_CID_AUTO_FOCUS_RANGE:
		if (ctrl->val == V4L2_AUTO_FOCUS_RANGE_MACRO) ret=T311_WRITE_TABLE_SEQ(cam,ISX012_AF_Macro_ON);
		else ret=T311_WRITE_TABLE_SEQ(cam,ISX012_AF_Macro_OFF);
		if (!ret) ret=T311_WRITE_TABLE_SEQ(cam,ISX012_AF_ReStart);
		break;
	case V4L2_CID_AUTO_FOCUS_START:
		ret=t311_isx012_apply_af_roi(cam);
		if (!ret) ret=T311_WRITE_TABLE_SEQ(cam,ISX012_AF_SAF);
		break;
	case V4L2_CID_AUTO_FOCUS_STOP:
		ret=T311_WRITE_TABLE_SEQ(cam,ISX012_AF_TouchSAF_OFF);
		break;
	case V4L2_CID_T311_AF_ROI_X:
	case V4L2_CID_T311_AF_ROI_Y:
		break;
	default:
		break;
	}
	return ret;
}

static int t311_isx012_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct t311_isx012 *cam = container_of(ctrl->handler, struct t311_isx012, ctrls);
	u32 state=0, result=0;
	int ret;
	if (ctrl->id != V4L2_CID_AUTO_FOCUS_STATUS)
		return -EINVAL;
	if (!cam->powered || !cam->initialized) {
		ctrl->val = V4L2_AUTO_FOCUS_STATUS_IDLE;
		return 0;
	}
	ret=t311_isx012_read(cam,T311_ISX012_REG_AF_STATE,&state,1); if(ret) return ret;
	if ((state & 0xff) != 8) {
		ctrl->val = V4L2_AUTO_FOCUS_STATUS_BUSY;
		return 0;
	}
	ret=t311_isx012_read(cam,T311_ISX012_REG_AF_RESULT,&result,1); if(ret) return ret;
	ctrl->val = ((result & 0xff) == 1) ? V4L2_AUTO_FOCUS_STATUS_REACHED : V4L2_AUTO_FOCUS_STATUS_FAILED;
	return 0;
}

static const struct v4l2_ctrl_ops t311_isx012_ctrl_ops = {
	.s_ctrl = t311_isx012_s_ctrl,
	.g_volatile_ctrl = t311_isx012_g_volatile_ctrl,
};

static const struct v4l2_ctrl_config t311_isx012_roi_x_cfg = {
	.ops=&t311_isx012_ctrl_ops, .id=V4L2_CID_T311_AF_ROI_X,
	.name="T311 AF ROI X", .type=V4L2_CTRL_TYPE_INTEGER,
	.min=0, .max=2591, .step=1, .def=1296,
};
static const struct v4l2_ctrl_config t311_isx012_roi_y_cfg = {
	.ops=&t311_isx012_ctrl_ops, .id=V4L2_CID_T311_AF_ROI_Y,
	.name="T311 AF ROI Y", .type=V4L2_CTRL_TYPE_INTEGER,
	.min=0, .max=1943, .step=1, .def=972,
};


static int t311_isx012_init_controls(struct t311_isx012 *cam)
{
	struct v4l2_ctrl_handler *h=&cam->ctrls;
	u64 wb_skip = ~((1ULL<<V4L2_WHITE_BALANCE_AUTO)|(1ULL<<V4L2_WHITE_BALANCE_INCANDESCENT)|
	                 (1ULL<<V4L2_WHITE_BALANCE_FLUORESCENT)|(1ULL<<V4L2_WHITE_BALANCE_DAYLIGHT)|
	                 (1ULL<<V4L2_WHITE_BALANCE_CLOUDY));
	u64 fx_skip = ~((1ULL<<V4L2_COLORFX_NONE)|(1ULL<<V4L2_COLORFX_BW)|(1ULL<<V4L2_COLORFX_SEPIA)|
	                 (1ULL<<V4L2_COLORFX_NEGATIVE)|(1ULL<<V4L2_COLORFX_SKETCH)|(1ULL<<V4L2_COLORFX_SOLARIZATION));
	int ret=v4l2_ctrl_handler_init(h,20); if(ret) return ret;
	h->lock = &cam->lock;
	v4l2_ctrl_new_std(h,&t311_isx012_ctrl_ops,V4L2_CID_BRIGHTNESS,-4,4,1,0);
	v4l2_ctrl_new_std(h,&t311_isx012_ctrl_ops,V4L2_CID_CONTRAST,-2,2,1,0);
	v4l2_ctrl_new_std(h,&t311_isx012_ctrl_ops,V4L2_CID_SATURATION,-2,2,1,0);
	v4l2_ctrl_new_std(h,&t311_isx012_ctrl_ops,V4L2_CID_SHARPNESS,-2,2,1,0);
	v4l2_ctrl_new_std_menu(h,&t311_isx012_ctrl_ops,V4L2_CID_EXPOSURE_METERING,V4L2_EXPOSURE_METERING_MATRIX,0,V4L2_EXPOSURE_METERING_MATRIX);
	cam->iso_auto=v4l2_ctrl_new_std_menu(h,&t311_isx012_ctrl_ops,V4L2_CID_ISO_SENSITIVITY_AUTO,V4L2_ISO_SENSITIVITY_AUTO,0,V4L2_ISO_SENSITIVITY_AUTO);
	cam->iso=v4l2_ctrl_new_int_menu(h,&t311_isx012_ctrl_ops,V4L2_CID_ISO_SENSITIVITY,ARRAY_SIZE(t311_isx012_iso_qmenu)-1,1,t311_isx012_iso_qmenu);
	v4l2_ctrl_new_std_menu(h,&t311_isx012_ctrl_ops,V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE,V4L2_WHITE_BALANCE_CLOUDY,wb_skip,V4L2_WHITE_BALANCE_AUTO);
	v4l2_ctrl_new_std_menu(h,&t311_isx012_ctrl_ops,V4L2_CID_COLORFX,V4L2_COLORFX_SOLARIZATION,fx_skip,V4L2_COLORFX_NONE);
	v4l2_ctrl_new_std_menu(h,&t311_isx012_ctrl_ops,V4L2_CID_SCENE_MODE,V4L2_SCENE_MODE_TEXT,0,V4L2_SCENE_MODE_NONE);
	v4l2_ctrl_new_std(h,&t311_isx012_ctrl_ops,V4L2_CID_3A_LOCK,0,V4L2_LOCK_EXPOSURE|V4L2_LOCK_WHITE_BALANCE,0,0);
	v4l2_ctrl_new_std_menu(h,&t311_isx012_ctrl_ops,V4L2_CID_AUTO_FOCUS_RANGE,V4L2_AUTO_FOCUS_RANGE_MACRO,~((1ULL<<V4L2_AUTO_FOCUS_RANGE_NORMAL)|(1ULL<<V4L2_AUTO_FOCUS_RANGE_MACRO)),V4L2_AUTO_FOCUS_RANGE_NORMAL);
	v4l2_ctrl_new_std(h,&t311_isx012_ctrl_ops,V4L2_CID_AUTO_FOCUS_START,0,1,1,0);
	v4l2_ctrl_new_std(h,&t311_isx012_ctrl_ops,V4L2_CID_AUTO_FOCUS_STOP,0,1,1,0);
	cam->af_status=v4l2_ctrl_new_std(h,&t311_isx012_ctrl_ops,V4L2_CID_AUTO_FOCUS_STATUS,0,V4L2_AUTO_FOCUS_STATUS_BUSY|V4L2_AUTO_FOCUS_STATUS_REACHED|V4L2_AUTO_FOCUS_STATUS_FAILED,0,V4L2_AUTO_FOCUS_STATUS_IDLE);
	if (cam->af_status) cam->af_status->flags |= V4L2_CTRL_FLAG_VOLATILE|V4L2_CTRL_FLAG_READ_ONLY;
	cam->af_roi_x=v4l2_ctrl_new_custom(h,&t311_isx012_roi_x_cfg,NULL);
	cam->af_roi_y=v4l2_ctrl_new_custom(h,&t311_isx012_roi_y_cfg,NULL);
	if (h->error) { ret=h->error; return ret; }
	cam->sd.ctrl_handler=h;
	return 0;
}

static int t311_isx012_get_frame_interval(struct v4l2_subdev *sd,
                                           struct v4l2_subdev_state *state,
                                           struct v4l2_subdev_frame_interval *fi)
{
	struct t311_isx012 *cam = to_t311_isx012(sd);
	(void)state;
	mutex_lock(&cam->lock);
	fi->interval.numerator = 1;
	fi->interval.denominator = cam->fps ? cam->fps : 30;
	mutex_unlock(&cam->lock);
	return 0;
}

static int t311_isx012_set_frame_interval(struct v4l2_subdev *sd,
                                           struct v4l2_subdev_state *state,
                                           struct v4l2_subdev_frame_interval *fi)
{
	struct t311_isx012 *cam = to_t311_isx012(sd);
	u32 fps;
	int ret = 0;
	(void)state;
	ret = 0;
	mutex_lock(&cam->lock);
	if (!fi->interval.numerator || !fi->interval.denominator)
		fps = 30;
	else
		fps = DIV_ROUND_CLOSEST(fi->interval.denominator, fi->interval.numerator);
	if (fps >= 28) fps = 30;
	else if (fps >= 20) fps = 25;
	else if (fps >= 11) fps = 15;
	else fps = 7;
	if (cam->fps == fps) {
		fi->interval.numerator = 1;
		fi->interval.denominator = fps;
		goto out;
	}
	if (cam->powered && cam->initialized) {
		if (fps == 30) ret = T311_WRITE_TABLE_SEQ(cam, isx012_fps_30fix);
		else if (fps == 25) ret = T311_WRITE_TABLE_SEQ(cam, isx012_fps_25fix);
		else if (fps == 15) ret = T311_WRITE_TABLE_SEQ(cam, isx012_fps_15fix);
		else ret = T311_WRITE_TABLE_SEQ(cam, isx012_fps_7fix);
	}
	if (!ret)
		cam->fps = fps;
	fi->interval.numerator = 1;
	fi->interval.denominator = cam->fps;
out:
	mutex_unlock(&cam->lock);
	return ret;
}

static const struct t311_isx012_mode *t311_isx012_find_mode(u32 width, u32 height)
{
	unsigned int i;
	for (i = 0; i < ARRAY_SIZE(t311_isx012_modes); i++)
		if (t311_isx012_modes[i].width == width && t311_isx012_modes[i].height == height)
			return &t311_isx012_modes[i];
	return &t311_isx012_modes[4];
}

static int t311_isx012_wait_streamoff(struct t311_isx012 *cam)
{
	u32 value = 0;
	int ret;
	int count;

	for (count = 0; count < 300; count++) {
		ret = t311_isx012_read(cam, 0x00dc, &value, 1);
		if (ret)
			return ret;
		if (!(value & 0x04))
			return 0;
		usleep_range(2000, 2500);
	}

	dev_warn(cam->dev, "SMT311_Q34P_ISX_STREAMOFF_WAIT timeout status=0x%02x\n",
		 value & 0xff);
	return 0;
}

static int t311_isx012_start_stream(struct t311_isx012 *cam)
{
	int ret;

	ret = t311_isx012_hw_init(cam);
	if (ret)
		return ret;
	{
		const struct t311_isx012_mode *mode=t311_isx012_find_mode(cam->fmt.width,cam->fmt.height);
		if (mode->capture) {
			ret=t311_isx012_write(cam,T311_ISX012_REG_HSIZE_CAP,cam->fmt.width,2); if(ret) return ret;
			ret=t311_isx012_write(cam,T311_ISX012_REG_VSIZE_CAP,cam->fmt.height,2); if(ret) return ret;
			ret=T311_WRITE_TABLE_SEQ(cam,ISX012_Capture_Mode); if(ret) return ret;
		} else {
			ret=t311_isx012_write(cam,T311_ISX012_REG_HSIZE_MONI,cam->fmt.width,2); if(ret) return ret;
			ret=t311_isx012_write(cam,T311_ISX012_REG_VSIZE_MONI,cam->fmt.height,2); if(ret) return ret;
			ret=T311_WRITE_TABLE_SEQ(cam,ISX012_Preview_Mode); if(ret) return ret;
		}
	}
	/* SMT311_Q34X_FPS_POST_MODE: mode tables may overwrite frame timing. */
	if (cam->fps == 30) ret = T311_WRITE_TABLE_SEQ(cam, isx012_fps_30fix);
	else if (cam->fps == 25) ret = T311_WRITE_TABLE_SEQ(cam, isx012_fps_25fix);
	else if (cam->fps == 15) ret = T311_WRITE_TABLE_SEQ(cam, isx012_fps_15fix);
	else if (cam->fps == 7) ret = T311_WRITE_TABLE_SEQ(cam, isx012_fps_7fix);
	if (ret) return ret;
	dev_info(cam->dev, "SMT311_Q34X_ISX_FPS_POST_MODE fps=%u %ux%u\n",
		 cam->fps, cam->fmt.width, cam->fmt.height);
	ret = t311_isx012_wait_irq(cam, T311_ISX012_INT_CM, 280);
	if (ret)
		return ret;
	ret = t311_isx012_write(cam, 0x00bf, 0x00, 1);
	if (!ret)
		dev_info(cam->dev, "PJARCZAK CAMERA H27: ISX012 STREAM ON %ux%u UYVY MIPI0\n",
			 cam->fmt.width, cam->fmt.height);
	return ret;
}

static int t311_isx012_s_power(struct v4l2_subdev *sd, int on)
{
	struct t311_isx012 *cam = to_t311_isx012(sd);

	/*
	 * HOTFIX9:
	 * FIMC calls s_power(1) from video-node open(). Android external
	 * provider opens the node repeatedly while only enumerating formats and
	 * static metadata. Power-cycling ISX012 for those metadata-only opens
	 * can leave the sensor warm/half-reset before the real STREAMON.
	 *
	 * Pad/format operations are purely cached in this driver and do not
	 * require hardware access, so physical rear-sensor power is deferred to
	 * s_stream(1). s_power(0) remains a safety cleanup for abnormal closes.
	 */
	mutex_lock(&cam->lock);
	if (!on) {
		t311_isx012_power_off(cam);
		cam->present = false;
		cam->streaming = false;
		dev_info(cam->dev, "SMT311_Q34P_ISX_POWER_OFF close=1\n");
	}
	mutex_unlock(&cam->lock);

	return 0;
}

static int t311_isx012_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct t311_isx012 *cam = to_t311_isx012(sd);
	int ret = 0;
	int attempt;

	mutex_lock(&cam->lock);

	if (!enable) {
		if (!cam->powered || !cam->streaming)
			goto out;

		ret = t311_isx012_write(cam, 0x00bf, 0x01, 1);
		if (!ret)
			ret = t311_isx012_wait_streamoff(cam);
		if (!ret) {
			cam->streaming = false;
			dev_info(cam->dev,
				 "SMT311_Q34P_ISX_WARM_STREAMOFF powered=1 initialized=%d\n",
				 cam->initialized);
		}
		goto out;
	}

	if (cam->streaming)
		goto out;

	if (cam->powered && cam->initialized) {
		ret = t311_isx012_start_stream(cam);
		if (!ret) {
			cam->present = true;
			cam->streaming = true;
			dev_info(cam->dev,
				 "SMT311_Q34P_ISX_WARM_STREAMON %ux%u fps=%u\n",
				 cam->fmt.width, cam->fmt.height, cam->fps);
			goto out;
		}
		dev_warn(cam->dev,
			 "SMT311_Q34P_ISX_WARM_STREAMON_FAIL ret=%d; cold recovery\n",
			 ret);
		t311_isx012_power_off(cam);
		cam->present = false;
		cam->streaming = false;
	}

	for (attempt = 0; attempt < 2; attempt++) {
		if (cam->powered)
			t311_isx012_power_off(cam);

		if (attempt)
			msleep(20);

		ret = t311_isx012_power_on(cam);
		if (ret)
			goto retry_or_out;

		ret = t311_isx012_start_stream(cam);
		if (!ret) {
			cam->present = true;
			cam->streaming = true;
			dev_info(cam->dev,
				 "SMT311_Q34P_ISX_COLD_STREAMON %ux%u fps=%u attempt=%d\n",
				 cam->fmt.width, cam->fmt.height, cam->fps, attempt + 1);
			goto out;
		}

retry_or_out:
		dev_warn(cam->dev,
			 "SMT311_Q34P_ISX_COLD_STREAMON_FAIL attempt=%d ret=%d retry=%s\n",
			 attempt + 1, ret, attempt == 0 ? "yes" : "no");
		t311_isx012_power_off(cam);
		cam->present = false;
		cam->streaming = false;
	}

out:
	mutex_unlock(&cam->lock);
	return ret;
}

static int t311_isx012_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_state *state, struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_UYVY8_2X8;
	return 0;
}

static int t311_isx012_enum_frame_size(struct v4l2_subdev *sd,
		struct v4l2_subdev_state *state, struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->pad || fse->code != MEDIA_BUS_FMT_UYVY8_2X8 ||
	    fse->index >= ARRAY_SIZE(t311_isx012_modes))
		return -EINVAL;
	fse->min_width = fse->max_width = t311_isx012_modes[fse->index].width;
	fse->min_height = fse->max_height = t311_isx012_modes[fse->index].height;
	return 0;
}

static int t311_isx012_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_state *state, struct v4l2_subdev_format *format)
{
	struct t311_isx012 *cam = to_t311_isx012(sd);
	if (format->pad)
		return -EINVAL;
	mutex_lock(&cam->lock);
	format->format = cam->fmt;
	mutex_unlock(&cam->lock);
	return 0;
}

static int t311_isx012_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_state *state, struct v4l2_subdev_format *format)
{
	struct t311_isx012 *cam = to_t311_isx012(sd);
	const struct t311_isx012_mode *mode;
	struct v4l2_mbus_framefmt *dst;

	if (format->pad)
		return -EINVAL;
	mode = t311_isx012_find_mode(format->format.width, format->format.height);
	format->format.width = mode->width;
	format->format.height = mode->height;
	format->format.code = MEDIA_BUS_FMT_UYVY8_2X8;
	format->format.field = V4L2_FIELD_NONE;
	format->format.colorspace = V4L2_COLORSPACE_SRGB;
	format->format.ycbcr_enc = V4L2_YCBCR_ENC_601;
	format->format.quantization = V4L2_QUANTIZATION_LIM_RANGE;
	format->format.xfer_func = V4L2_XFER_FUNC_SRGB;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		dst = v4l2_subdev_state_get_format(state, format->pad);
		*dst = format->format;
	} else {
		mutex_lock(&cam->lock);
		cam->fmt = format->format;
		mutex_unlock(&cam->lock);
	}
	return 0;
}

static const struct v4l2_subdev_core_ops t311_isx012_core_ops = { .s_power = t311_isx012_s_power };
static const struct v4l2_subdev_video_ops t311_isx012_video_ops = {
	.s_stream = t311_isx012_s_stream,
};
static const struct v4l2_subdev_pad_ops t311_isx012_pad_ops = {
	.enum_mbus_code = t311_isx012_enum_mbus_code,
	.enum_frame_size = t311_isx012_enum_frame_size,
	.get_fmt = t311_isx012_get_fmt,
	.set_fmt = t311_isx012_set_fmt,
	.get_frame_interval = t311_isx012_get_frame_interval,
	.set_frame_interval = t311_isx012_set_frame_interval,
};
static const struct v4l2_subdev_ops t311_isx012_ops = {
	.core = &t311_isx012_core_ops,
	.video = &t311_isx012_video_ops,
	.pad = &t311_isx012_pad_ops,
};

static int t311_isx012_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct t311_isx012 *cam;
	int ret, detect_ret;

	cam = devm_kzalloc(dev, sizeof(*cam), GFP_KERNEL);
	if (!cam)
		return -ENOMEM;
	cam->dev = dev;
	mutex_init(&cam->lock);
	cam->core = devm_regulator_get(dev, "core");
	if (IS_ERR(cam->core)) return dev_err_probe(dev, PTR_ERR(cam->core), "missing core supply\n");
	cam->io = devm_regulator_get(dev, "io");
	if (IS_ERR(cam->io)) return dev_err_probe(dev, PTR_ERR(cam->io), "missing io supply\n");
	cam->analog = devm_regulator_get(dev, "analog");
	if (IS_ERR(cam->analog)) return dev_err_probe(dev, PTR_ERR(cam->analog), "missing analog supply\n");
	cam->vt = devm_regulator_get(dev, "vt");
	if (IS_ERR(cam->vt)) return dev_err_probe(dev, PTR_ERR(cam->vt), "missing vt supply\n");
	cam->af = devm_regulator_get(dev, "af");
	if (IS_ERR(cam->af)) return dev_err_probe(dev, PTR_ERR(cam->af), "missing af supply\n");
	cam->xclk = devm_clk_get(dev, "xclk");
	if (IS_ERR(cam->xclk)) return dev_err_probe(dev, PTR_ERR(cam->xclk), "missing xclk\n");
	cam->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(cam->pinctrl)) return dev_err_probe(dev, PTR_ERR(cam->pinctrl), "missing sensor pinctrl\n");
	cam->pins_mclk_active = pinctrl_lookup_state(cam->pinctrl, "mclk-active");
	if (IS_ERR(cam->pins_mclk_active)) return dev_err_probe(dev, PTR_ERR(cam->pins_mclk_active), "missing mclk-active pinctrl state\n");
	cam->pins_mclk_idle = pinctrl_lookup_state(cam->pinctrl, "mclk-idle");
	if (IS_ERR(cam->pins_mclk_idle)) return dev_err_probe(dev, PTR_ERR(cam->pins_mclk_idle), "missing mclk-idle pinctrl state\n");
	ret = pinctrl_select_state(cam->pinctrl, cam->pins_mclk_idle);
	if (ret) return dev_err_probe(dev, ret, "failed selecting initial MCLK idle state\n");
	cam->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(cam->reset)) return dev_err_probe(dev, PTR_ERR(cam->reset), "missing reset gpio\n");
	cam->standby = devm_gpiod_get(dev, "standby", GPIOD_OUT_HIGH);
	if (IS_ERR(cam->standby)) return dev_err_probe(dev, PTR_ERR(cam->standby), "missing standby gpio\n");
	cam->vt_standby = devm_gpiod_get(dev, "vt-standby", GPIOD_OUT_HIGH | GPIOD_FLAGS_BIT_NONEXCLUSIVE);
	if (IS_ERR(cam->vt_standby)) return dev_err_probe(dev, PTR_ERR(cam->vt_standby), "missing shared VT standby gpio\n");
	cam->vt_reset = devm_gpiod_get(dev, "vt-reset", GPIOD_OUT_HIGH | GPIOD_FLAGS_BIT_NONEXCLUSIVE);
	if (IS_ERR(cam->vt_reset)) return dev_err_probe(dev, PTR_ERR(cam->vt_reset), "missing shared VT reset gpio\n");

	v4l2_i2c_subdev_init(&cam->sd, client, &t311_isx012_ops);
	ret = t311_isx012_init_controls(cam);
	if (ret) goto err_mutex;
	cam->fps = 0;
	cam->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	cam->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	cam->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&cam->sd.entity, 1, &cam->pad);
	if (ret)
		goto err_mutex;
	cam->fmt.width = 640;
	cam->fmt.height = 480;
	cam->fmt.code = MEDIA_BUS_FMT_UYVY8_2X8;
	cam->fmt.field = V4L2_FIELD_NONE;
	cam->fmt.colorspace = V4L2_COLORSPACE_SRGB;
	cam->fmt.ycbcr_enc = V4L2_YCBCR_ENC_601;
	cam->fmt.quantization = V4L2_QUANTIZATION_LIM_RANGE;
	cam->fmt.xfer_func = V4L2_XFER_FUNC_SRGB;

	ret = v4l2_subdev_init_finalize(&cam->sd);
	if (ret)
		goto err_entity;

	/* Stock LT01 defers sensor communication until the camera is opened. */
	cam->present = false;
	detect_ret = 0;
	dev_info(dev,
		 "PJARCZAK CAMERA HOTFIX9: ISX012 subdev registered; physical power deferred until STREAMON xclk=%lu\n",
		 T311_ISX012_XCLK);

	ret = v4l2_async_register_subdev_sensor(&cam->sd);
	if (ret)
		goto err_state;
	return 0;

err_state:
	v4l2_subdev_cleanup(&cam->sd);
err_entity:
	media_entity_cleanup(&cam->sd.entity);
err_mutex:
	v4l2_ctrl_handler_free(&cam->ctrls);
	mutex_destroy(&cam->lock);
	return ret;
}

static void t311_isx012_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct t311_isx012 *cam = to_t311_isx012(sd);
	 v4l2_async_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&cam->ctrls);
	mutex_lock(&cam->lock);
	t311_isx012_power_off(cam);
	mutex_unlock(&cam->lock);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	mutex_destroy(&cam->lock);
}

static const struct of_device_id t311_isx012_of_match[] = {
	{ .compatible = "sony,isx012-t311" }, { }
};
MODULE_DEVICE_TABLE(of, t311_isx012_of_match);
static const struct i2c_device_id t311_isx012_id[] = {
	{ "isx012-t311" }, { }
};
MODULE_DEVICE_TABLE(i2c, t311_isx012_id);
static struct i2c_driver t311_isx012_driver = {
	.driver = { .name = "t311-isx012", .of_match_table = t311_isx012_of_match },
	.probe = t311_isx012_probe,
	.remove = t311_isx012_remove,
	.id_table = t311_isx012_id,
};
module_i2c_driver(t311_isx012_driver);
MODULE_DESCRIPTION("SM-T311 Sony ISX012 modern V4L2 driver with LT01 setfile");
MODULE_AUTHOR("PJARCZAK bring-up");
MODULE_LICENSE("GPL");
