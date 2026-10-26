// SPDX-License-Identifier: GPL-2.0
/*
 * Samsung S6D7AA0 MIPI-DSI TFT LCD controller drm_panel driver.
 *
 * Copyright (C) 2022 Artur Weber <aweber.kernel@gmail.com>
 */

#include <linux/atomic.h>
#include <linux/backlight.h>
#include <linux/log2.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/of.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/workqueue.h>

#ifdef CONFIG_ARM
#include <asm/system_info.h>
#endif

#include <video/mipi_display.h>
#include <drm/bridge/samsung-dsim.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

/* Manufacturer command set */
#define MCS_BL_CTL		0xc3
#define MCS_OTP_RELOAD		0xd0
#define MCS_PASSWD1		0xf0
#define MCS_PASSWD2		0xf1
#define MCS_PASSWD3		0xfc

#define mipi_dsi_dcs_write_seq(device, cmd, seq...) \
	do { \
		static const u8 payload[] = { cmd, seq }; \
		int rc = mipi_dsi_dcs_write_buffer(device, payload, ARRAY_SIZE(payload)); \
		if (rc < 0) \
			return rc; \
	} while (0)

struct s6d7aa0 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[2];
	const struct s6d7aa0_panel_desc *desc;
	bool smt311_powered;
	bool smt311_reset_done;
	u8 smt311_id[3];
	bool smt311_id_read;
	bool smt311_id_retry_blocked;
	bool smt311_connected;
	struct gpio_desc *smt311_error_gpio;
	struct delayed_work smt311_error_work;
	int smt311_error_irq;
	unsigned int smt311_error_count;
	unsigned int smt311_recovery_count;
	atomic_t smt311_error_edge_count;
	bool smt311_error_monitor_enabled;
	bool smt311_error_irq_enabled;
};

struct s6d7aa0_panel_desc {
	unsigned int panel_type;
	int (*init_func)(struct s6d7aa0 *ctx);
	int (*off_func)(struct s6d7aa0 *ctx);
	const struct drm_display_mode *drm_mode;
	unsigned long mode_flags;
	u32 bus_flags;
	bool has_backlight;
	bool use_passwd3;
};

enum s6d7aa0_panels {
	S6D7AA0_PANEL_LSL080AL02,
	S6D7AA0_PANEL_LSL080AL03,
	S6D7AA0_PANEL_LTL101AT01,
};

static inline struct s6d7aa0 *panel_to_s6d7aa0(struct drm_panel *panel)
{
	return container_of(panel, struct s6d7aa0, panel);
}

static void s6d7aa0_reset(struct s6d7aa0 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	msleep(50);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(50);
}

static bool s6d7aa0_smt311_powered(void *data)
{
	struct s6d7aa0 *ctx = data;

	return ctx->smt311_powered &&
		regulator_is_enabled(ctx->supplies[0].consumer) > 0 &&
		regulator_is_enabled(ctx->supplies[1].consumer) > 0;
}

static int s6d7aa0_smt311_reset_after_link(void *data)
{
	struct s6d7aa0 *ctx = data;
	struct device *dev = &ctx->dsi->dev;

	if (!s6d7aa0_smt311_powered(ctx))
		return -EHOSTDOWN;

	/* reset-gpios is active-low: logical 0/1/0 is physical high/low/high. */
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(5000, 5500);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 5500);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(5000, 5500);
	ctx->smt311_reset_done = true;
	dev_info(dev,
		 "SMT311_V128_PANEL_STOCK_RESET_AFTER_DSIM_LINK high_ms=5 low_ms=5 high_ms2=5 reset_deasserted=%d\n",
		 !gpiod_get_value_cansleep(ctx->reset_gpio));
	return 0;
}


/* Stock SM-T311 ERR_FG recovery. The signal is active-low GPF0(7).
 * On the DT-based Linux 6.12 port, presence of error-gpios represents the
 * downstream system_rev >= 2 hardware gate. */
#define SMT311_ERR_FG_FILTER_SAMPLES 10

static int s6d7aa0_on(struct s6d7aa0 *ctx);
static int s6d7aa0_off(struct s6d7aa0 *ctx);
static int s6d7aa0_smt311_off(struct s6d7aa0 *ctx);

static int s6d7aa0_smt311_set_error_bias(struct s6d7aa0 *ctx,
					enum pin_config_param param,
					const char *marker)
{
	int ret;
	u32 argument = param == PIN_CONFIG_BIAS_DISABLE ? 0 : 1;

	if (!ctx->smt311_error_gpio)
		return 0;
	ret = gpiod_set_config(ctx->smt311_error_gpio,
				 pinconf_to_config_packed(param, argument));
	if (ret) {
		dev_err(&ctx->dsi->dev, "%s failed ret=%d\n", marker, ret);
		return ret;
	}
	dev_info(&ctx->dsi->dev, "%s\n", marker);
	return 0;
}

static void s6d7aa0_smt311_stop_error_monitor(struct s6d7aa0 *ctx)
{
	if (!ctx->smt311_error_gpio)
		return;

	WRITE_ONCE(ctx->smt311_error_monitor_enabled, false);
	if (ctx->smt311_error_irq_enabled) {
		disable_irq(ctx->smt311_error_irq);
		ctx->smt311_error_irq_enabled = false;
	}
	cancel_delayed_work_sync(&ctx->smt311_error_work);
	ctx->smt311_error_count = 0;
	if (s6d7aa0_smt311_set_error_bias(ctx, PIN_CONFIG_BIAS_PULL_DOWN,
					     "SMT311_V130_ERR_FG_BIAS_PULL_DOWN"))
		dev_warn(&ctx->dsi->dev, "stock ERR_FG pull-down could not be restored\n");
}

static int s6d7aa0_smt311_start_error_monitor(struct s6d7aa0 *ctx)
{
	int ret;

	if (!ctx->smt311_error_gpio)
		return 0;
	ret = s6d7aa0_smt311_set_error_bias(ctx, PIN_CONFIG_BIAS_PULL_UP,
					    "SMT311_V130_ERR_FG_BIAS_PULL_UP");
	if (ret)
		return ret;

	ctx->smt311_error_count = 0;
	WRITE_ONCE(ctx->smt311_error_monitor_enabled, true);
	if (!ctx->smt311_error_irq_enabled) {
		enable_irq(ctx->smt311_error_irq);
		ctx->smt311_error_irq_enabled = true;
	}
	return 0;
}

static void s6d7aa0_smt311_error_work(struct work_struct *work)
{
	struct s6d7aa0 *ctx = container_of(to_delayed_work(work),
					      struct s6d7aa0, smt311_error_work);
	struct device *dev = &ctx->dsi->dev;
	int asserted;
	int ret;

	if (!READ_ONCE(ctx->smt311_error_monitor_enabled) ||
	    !ctx->smt311_error_gpio || !ctx->smt311_powered ||
	    !ctx->smt311_reset_done ||
	    !samsung_dsim_smt311_pipeline_active(ctx->dsi))
		return;

	/* error-gpios is active-low, therefore logical 1 means asserted. */
	asserted = gpiod_get_value_cansleep(ctx->smt311_error_gpio);
	if (asserted < 0) {
		dev_err(dev, "SMT311_V130_ERR_FG_READ_FAILED ret=%d\n", asserted);
		return;
	}
	if (!asserted) {
		ctx->smt311_error_count = 0;
		return;
	}

	ctx->smt311_error_count++;
	dev_warn(dev,
		 "SMT311_V130_ERR_FG_ASSERTED sample=%u recovery=%u\n",
		 ctx->smt311_error_count, ctx->smt311_recovery_count);
	if (ctx->smt311_error_count < SMT311_ERR_FG_FILTER_SAMPLES) {
		mod_delayed_work(system_wq, &ctx->smt311_error_work, HZ / 8);
		return;
	}

	/* Stock power_off disables ERR_FG before DCS off; mirror it while the
	 * recovery transaction runs, then restore monitoring after power_on. */
	WRITE_ONCE(ctx->smt311_error_monitor_enabled, false);
	if (ctx->smt311_error_irq_enabled) {
		disable_irq_nosync(ctx->smt311_error_irq);
		ctx->smt311_error_irq_enabled = false;
	}
	ctx->smt311_error_count = 0;
	ctx->smt311_recovery_count++;
	if (s6d7aa0_smt311_set_error_bias(ctx, PIN_CONFIG_BIAS_PULL_DOWN,
					     "SMT311_V130_ERR_FG_BIAS_PULL_DOWN")) {
		dev_err(dev, "SMT311_V130_ERR_FG_RECOVERY_BIAS_DOWN_FAILED\n");
		return;
	}
	dev_warn(dev, "SMT311_V130_ERR_FG_RECOVERY_BEGIN attempt=%u\n",
		 ctx->smt311_recovery_count);

	ret = s6d7aa0_off(ctx);
	if (ret < 0) {
		dev_err(dev, "SMT311_V130_ERR_FG_RECOVERY_OFF_FAILED ret=%d\n", ret);
		return;
	}
	ret = s6d7aa0_on(ctx);
	if (ret < 0) {
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		ctx->smt311_reset_done = false;
		dev_err(dev, "SMT311_V130_ERR_FG_RECOVERY_ON_FAILED ret=%d reset_asserted=1\n", ret);
		return;
	}

	if (s6d7aa0_smt311_set_error_bias(ctx, PIN_CONFIG_BIAS_PULL_UP,
					    "SMT311_V130_ERR_FG_BIAS_PULL_UP")) {
		dev_err(dev, "SMT311_V130_ERR_FG_RECOVERY_BIAS_UP_FAILED\n");
		return;
	}
	WRITE_ONCE(ctx->smt311_error_monitor_enabled, true);
	enable_irq(ctx->smt311_error_irq);
	ctx->smt311_error_irq_enabled = true;
	dev_info(dev, "SMT311_V130_ERR_FG_RECOVERY_DONE attempt=%u\n",
		 ctx->smt311_recovery_count);
}

/*
 * IRQF_TRIGGER_FALLING captures a potential active-low ERR_FG event at the
 * GPIO controller, even if the pin has returned high by the delayed work's
 * first sample (~HZ/16). Keep the existing 10-sample recovery filter intact.
 * Warn only on power-of-two edges to avoid logging/IRQ amplification.
 */
static irqreturn_t s6d7aa0_smt311_error_irq_thread(int irq, void *data)
{
	struct s6d7aa0 *ctx = data;

	if (READ_ONCE(ctx->smt311_error_monitor_enabled)) {
		unsigned int edges = atomic_inc_return(&ctx->smt311_error_edge_count);

		if (is_power_of_2(edges))
			dev_warn(&ctx->dsi->dev,
				 "SMT311_ERR_FG_EDGE_COUNT=%u recovery_count=%u monitor_active=1\n",
				 edges, ctx->smt311_recovery_count);
		mod_delayed_work(system_wq, &ctx->smt311_error_work, HZ / 16);
	}
	return IRQ_HANDLED;
}

static int s6d7aa0_lock(struct s6d7aa0 *ctx, bool lock)
{
	struct mipi_dsi_device *dsi = ctx->dsi;

	if (lock) {
		mipi_dsi_dcs_write_seq(dsi, MCS_PASSWD1, 0xa5, 0xa5);
		mipi_dsi_dcs_write_seq(dsi, MCS_PASSWD2, 0xa5, 0xa5);
		if (ctx->desc->use_passwd3)
			mipi_dsi_dcs_write_seq(dsi, MCS_PASSWD3, 0x5a, 0x5a);
	} else {
		mipi_dsi_dcs_write_seq(dsi, MCS_PASSWD1, 0x5a, 0x5a);
		mipi_dsi_dcs_write_seq(dsi, MCS_PASSWD2, 0x5a, 0x5a);
		if (ctx->desc->use_passwd3)
			mipi_dsi_dcs_write_seq(dsi, MCS_PASSWD3, 0xa5, 0xa5);
	}

	return 0;
}

static int s6d7aa0_on(struct s6d7aa0 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	ret = ctx->desc->init_func(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		return ret;
	}

	/* LSL080AL02 init_func contains the single downstream DISPLAY_ON. */
	if (ctx->desc->panel_type == S6D7AA0_PANEL_LSL080AL02)
		return 0;

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}

	return 0;
}

static int s6d7aa0_off(struct s6d7aa0 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	if (ctx->desc->panel_type == S6D7AA0_PANEL_LSL080AL02)
		return s6d7aa0_smt311_off(ctx);

	ret = ctx->desc->off_func(ctx);
	if (ret < 0) {
		dev_err(dev, "Panel-specific off function failed: %d\n", ret);
		return ret;
	}
	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0)
		return ret;
	msleep(64);
	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0)
		return ret;
	msleep(120);
	return 0;
}

static int s6d7aa0_prepare(struct drm_panel *panel)
{
	struct s6d7aa0 *ctx = panel_to_s6d7aa0(panel);
	struct device *dev = &ctx->dsi->dev;
	bool smt311 = ctx->desc->panel_type == S6D7AA0_PANEL_LSL080AL02 &&
		of_machine_is_compatible("samsung,tab3");
	int ret;

	if (smt311 && !samsung_dsim_smt311_host_power_ready(ctx->dsi)) {
		dev_err(dev,
			"SMT311_V128_PANEL_PREPARE_REJECTED_HOST_POWER_NOT_READY\n");
		return -EHOSTDOWN;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	if (smt311) {
		/* Stock keeps reset asserted while LCD power settles, then DSIM creates
		 * the link and invokes the exact high/low/high reset callback. */
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		ctx->smt311_powered = true;
		ctx->smt311_reset_done = false;
		msleep(25);
		dev_info(dev,
			 "SMT311_V128_PANEL_POWER_READY_BEFORE_DSIM_LINK power_on=%d power_uv=%d vmipi_on=%d vmipi_uv=%d reset_asserted=%d settle_ms=25\n",
			 regulator_is_enabled(ctx->supplies[0].consumer),
			 regulator_get_voltage(ctx->supplies[0].consumer),
			 regulator_is_enabled(ctx->supplies[1].consumer),
			 regulator_get_voltage(ctx->supplies[1].consumer),
			 gpiod_get_value_cansleep(ctx->reset_gpio));
		return 0;
	}

	s6d7aa0_reset(ctx);
	ret = s6d7aa0_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
		return ret;
	}
	return 0;
}

static int s6d7aa0_enable(struct drm_panel *panel)
{
	struct s6d7aa0 *ctx = panel_to_s6d7aa0(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (ctx->desc->panel_type != S6D7AA0_PANEL_LSL080AL02)
		return 0;
	if (of_machine_is_compatible("samsung,tab3") &&
	    (!ctx->smt311_powered || !ctx->smt311_reset_done ||
	     !samsung_dsim_smt311_pipeline_active(ctx->dsi))) {
		dev_err(dev,
			"SMT311_V127_PANEL_ENABLE_REJECTED_PIPELINE_NOT_ACTIVE powered=%d reset_done=%d\n",
			ctx->smt311_powered, ctx->smt311_reset_done);
		return -EHOSTDOWN;
	}

	ret = s6d7aa0_on(ctx);
	if (ret < 0) {
		s6d7aa0_smt311_stop_error_monitor(ctx);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		ctx->smt311_reset_done = false;
		dev_err(dev,
			"SMT311_V130_PANEL_ENABLE_FAILURE_STATE_CLEAN ret=%d reset_asserted=1 reset_done=0\n",
			ret);
		return ret;
	}

	ret = s6d7aa0_smt311_start_error_monitor(ctx);
	if (ret) {
		int off_ret = s6d7aa0_off(ctx);

		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		ctx->smt311_reset_done = false;
		dev_err(dev,
			"SMT311_V130_PANEL_ENABLE_FAILURE_STATE_CLEAN ret=%d off_ret=%d reset_asserted=1 reset_done=0\n",
			ret, off_ret);
		return ret;
	}
	dev_info(dev,
		 "SMT311_V128_PANEL_STOCK_ID_SEQUENCE_COMPLETE_AFTER_FIMD\n");
	return 0;
}

static int s6d7aa0_disable(struct drm_panel *panel)
{
	struct s6d7aa0 *ctx = panel_to_s6d7aa0(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	s6d7aa0_smt311_stop_error_monitor(ctx);
	ret = s6d7aa0_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	return ret;
}

static int s6d7aa0_unprepare(struct drm_panel *panel)
{
	struct s6d7aa0 *ctx = panel_to_s6d7aa0(panel);
	int ret;

	s6d7aa0_smt311_stop_error_monitor(ctx);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	ctx->smt311_reset_done = false;
	ret = regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret) {
		dev_err(&ctx->dsi->dev,
			"SMT311_V130_PANEL_UNPREPARE_REGULATOR_DISABLE_FAILED ret=%d\n",
			ret);
		return ret;
	}
	ctx->smt311_powered = false;
	/* A failed ID transaction can only be retried after this real power-off. */
	ctx->smt311_id_retry_blocked = false;
	dev_info(&ctx->dsi->dev,
		 "SMT311_V127_PANEL_POWER_OFF reset_asserted=%d power_on=%d vmipi_on=%d\n",
		 gpiod_get_value_cansleep(ctx->reset_gpio),
		 regulator_is_enabled(ctx->supplies[0].consumer),
		 regulator_is_enabled(ctx->supplies[1].consumer));
	return 0;
}

/* Backlight control code */

static int s6d7aa0_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	ret = mipi_dsi_dcs_set_display_brightness(dsi, brightness);
	if (ret < 0)
		return ret;

	return 0;
}

static int s6d7aa0_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness;
	int ret;

	ret = mipi_dsi_dcs_get_display_brightness(dsi, &brightness);
	if (ret < 0)
		return ret;

	return brightness & 0xff;
}

static const struct backlight_ops s6d7aa0_bl_ops = {
	.update_status = s6d7aa0_bl_update_status,
	.get_brightness = s6d7aa0_bl_get_brightness,
};

static struct backlight_device *
s6d7aa0_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 255,
		.max_brightness = 255,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &s6d7aa0_bl_ops, &props);
}

/* Initialization code and structures for LSL080AL02 panel */

static int s6d7aa0_smt311_write(struct s6d7aa0 *ctx,
				       const u8 *seq, size_t len)
{
	ssize_t ret = -EIO;
	int attempt;

	for (attempt = 0; attempt < 2; attempt++) {
		ret = mipi_dsi_dcs_write_buffer(ctx->dsi, seq, len);
		/* Downstream _s6d7aa0_write() always leaves 100 us quiet time,
		 * including failed attempts, before its one retry. */
		usleep_range(100, 150);
		if (ret == len) {
			dev_dbg(&ctx->dsi->dev,
				"SMT311_V130_PANEL_DCS_ATTEMPT_QUIET_TIME cmd=%02x attempt=%d\n",
				seq[0], attempt + 1);
			return 0;
		}
	}
	dev_err(&ctx->dsi->dev,
		"stock DCS 0x%02x failed or short-written: ret=%zd expected=%zu\n",
		seq[0], ret, len);
	return ret < 0 ? (int)ret : -EIO;
}

#define SMT311_WRITE(ctx, name) do { \
	int __smt311_ret = s6d7aa0_smt311_write((ctx), (name), ARRAY_SIZE(name)); \
	if (__smt311_ret) return __smt311_ret; \
} while (0)

static const u8 smt311_passwd1[] = { 0xf0, 0x5a, 0x5a };
static const u8 smt311_passwd2[] = { 0xf1, 0x5a, 0x5a };
static const u8 smt311_passwd3[] = { 0xfc, 0xa5, 0xa5 };
static const u8 smt311_passwd1_lock[] = { 0xf0, 0xa5, 0xa5 };
static const u8 smt311_passwd2_lock[] = { 0xf1, 0xa5, 0xa5 };
static const u8 smt311_panel_ctl[] = { 0xb1, 0x10 };
static const u8 smt311_interface_ctl[] = { 0xb2, 0x14, 0x22, 0x2f, 0x00 };
static const u8 smt311_otp_reload[] = { 0xd0, 0x00, 0x10 };
static const u8 smt311_disp_ctl[] = { 0xf2, 0x02, 0x08, 0x08, 0x90, 0x10 };
static const u8 smt311_dad_ctl[] = { 0x36, 0x04 };
static const u8 smt311_dad_ctl_lr[] = { 0x36, 0x00 };
static const u8 smt311_global_param[] = { 0xb0, 0x04 };
static const u8 smt311_b6_id8c[] = { 0xb6, 0x10 };
static const u8 smt311_rfd_ctl[] = { 0xfd, 0x09 };
static const u8 smt311_pwr_ctl0[] = { 0xf3, 0x01, 0xc0, 0xe0, 0x62, 0xd3, 0x81, 0x35, 0x34, 0x01, 0x00 };
static const u8 smt311_pwr_ctl1[] = {
	0xf4, 0x00, 0x02, 0x03, 0x26, 0x03, 0x02, 0x09, 0x00, 0x07, 0x16,
	0x16, 0x03, 0x00, 0x08, 0x08, 0x03, 0x19, 0x1a, 0x12, 0x1c,
	0x1d, 0x1e, 0x1a, 0x09, 0x01, 0x04, 0x02, 0x61, 0x74, 0x75,
	0x72, 0x83, 0x80, 0x80, 0x00, 0x00, 0x01, 0x01, 0x28, 0x04,
	0x03, 0x28, 0x01, 0xd1, 0x32
};
static const u8 smt311_pwr_ctl2[] = {
	0xf5, 0x64, 0x28, 0x28, 0x5f, 0xab, 0x98, 0x4f, 0x0f, 0x33, 0x43,
	0x04, 0x59, 0x54, 0x52, 0x05, 0x40, 0x40, 0x5d, 0x59, 0x40,
	0x27, 0x26, 0x41, 0x14, 0x4d, 0x18
};
static const u8 smt311_asg_ctl0[] = { 0xee, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const u8 smt311_asg_ctl1[] = { 0xef, 0x12, 0x12, 0x43, 0x43, 0x90, 0x84, 0x24, 0x81 };
static const u8 smt311_asg_ctl2[] = { 0xf6, 0x63, 0x25, 0xa6, 0x0a, 0x0c, 0x14 };
static const u8 smt311_pgamma[] = {
	0xfa, 0x00, 0x34, 0x01, 0x05, 0x0e, 0x07, 0x0c, 0x12, 0x14,
	0x1c, 0x23, 0x2b, 0x34, 0x35, 0x2e, 0x2d, 0x30
};
static const u8 smt311_ngamma[] = {
	0xfb, 0x00, 0x34, 0x01, 0x05, 0x0e, 0x07, 0x0c, 0x12, 0x14,
	0x1c, 0x23, 0x2b, 0x34, 0x35, 0x2e, 0x2d, 0x30
};
static const u8 smt311_gout[] = {
	0xf7, 0x0a, 0x0a, 0x08, 0x08, 0x0b, 0x0b, 0x09, 0x09, 0x04,
	0x05, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x0a, 0x0a, 0x08,
	0x08, 0x0b, 0x0b, 0x09, 0x09, 0x04, 0x05, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01
};
static const u8 smt311_bl_on[] = { 0xc3, 0x40, 0x00, 0x28 };
static const u8 smt311_sleep_out[] = { 0x11 };
static const u8 smt311_display_on[] = { 0x29 };
static const u8 smt311_display_off[] = { 0x28 };
static const u8 smt311_bl_off[] = { 0xc3, 0x40, 0x00, 0x20 };
static const u8 smt311_sleep_in[] = { 0x10 };

static int s6d7aa0_smt311_off(struct s6d7aa0 *ctx)
{
	SMT311_WRITE(ctx, smt311_display_off);
	msleep(64);
	SMT311_WRITE(ctx, smt311_bl_off);
	usleep_range(1000, 1500);
	SMT311_WRITE(ctx, smt311_sleep_in);
	msleep(120);
	msleep(135);
	dev_info(&ctx->dsi->dev,
		 "SMT311_V130_PANEL_STOCK_OFF_TRANSPORT retries=1 quiet_us=100 sequence=28_c3_10\n");
	dev_info(&ctx->dsi->dev,
		 "SMT311_V127_PANEL_DOWNSTREAM_POWER_OFF_SEQUENCE_COMPLETE\n");
	return 0;
}

static int s6d7aa0_smt311_read_id(struct s6d7aa0 *ctx)
{
	u8 addr;
	int i, attempt;
	ssize_t ret = -EIO;

	/* Samsung caches a complete successful ID.  A failed transaction is not
	 * treated as ID8C and is not retried until unprepare has performed a real
	 * power cycle. */
	if (ctx->smt311_id_read)
		return ctx->smt311_connected ? 0 : -ENODEV;
	if (ctx->smt311_id_retry_blocked)
		return -EAGAIN;

	memset(ctx->smt311_id, 0, sizeof(ctx->smt311_id));
	for (i = 0; i < ARRAY_SIZE(ctx->smt311_id); i++) {
		addr = 0xda + i;
		for (attempt = 0; attempt < 3; attempt++) {
			/* Downstream sends SET_MAXIMUM_RETURN_PACKET_SIZE=1 before every
			 * GENERIC_READ_REQUEST_1_PARAM for ID registers 0xDA..0xDC. */
			ret = mipi_dsi_set_maximum_return_packet_size(ctx->dsi, 1);
			if (!ret)
				ret = mipi_dsi_generic_read(ctx->dsi, &addr, 1,
							    &ctx->smt311_id[i], 1);
			usleep_range(100, 150);
			if (ret == 1)
				break;
		}
		if (ret != 1) {
			memset(ctx->smt311_id, 0, sizeof(ctx->smt311_id));
			ctx->smt311_connected = false;
			ctx->smt311_id_retry_blocked = true;
			dev_err(&ctx->dsi->dev,
				"SMT311_V130_PANEL_ID_RETRY_AFTER_POWER_CYCLE reg=%02x ret=%zd connected=0\n",
				addr, ret);
			return -ENODEV;
		}
	}

	ctx->smt311_id_read = true;
	ctx->smt311_id_retry_blocked = false;
	ctx->smt311_connected = true;
	dev_info(&ctx->dsi->dev,
		 "SMT311_V130_PANEL_ID_GENERIC_READ request=generic_1param max_return=1 retries=2 da=%02x db=%02x dc=%02x\n",
		 ctx->smt311_id[0], ctx->smt311_id[1], ctx->smt311_id[2]);
	dev_info(&ctx->dsi->dev,
		 "SMT311_V129_PANEL_ID_CACHED_ONCE da=%02x db=%02x dc=%02x connected=1\n",
		 ctx->smt311_id[0], ctx->smt311_id[1], ctx->smt311_id[2]);
	return 0;
}

static int s6d7aa0_lsl080al02_init(struct s6d7aa0 *ctx)
{
	struct device *dev = &ctx->dsi->dev;
	const char *profile;
	int ret;

	usleep_range(20000, 20500);
	ret = s6d7aa0_smt311_read_id(ctx);
	if (ret)
		return ret;

	SMT311_WRITE(ctx, smt311_passwd1);
	SMT311_WRITE(ctx, smt311_passwd2);

	if (ctx->smt311_id[1] == 0x8c) {
		profile = "ID8C";
		SMT311_WRITE(ctx, smt311_otp_reload);
		usleep_range(1000, 1500);
		SMT311_WRITE(ctx, smt311_b6_id8c);
		SMT311_WRITE(ctx, smt311_bl_on);
		usleep_range(5000, 5500);
		SMT311_WRITE(ctx, smt311_dad_ctl);
		SMT311_WRITE(ctx, smt311_sleep_out);
		msleep(120);
		SMT311_WRITE(ctx, smt311_dad_ctl_lr);
	} else if (ctx->smt311_id[1] == 0x00) {
		profile = "ID00_FULL";
		SMT311_WRITE(ctx, smt311_passwd3);
		SMT311_WRITE(ctx, smt311_panel_ctl);
		SMT311_WRITE(ctx, smt311_interface_ctl);
		SMT311_WRITE(ctx, smt311_otp_reload);
		SMT311_WRITE(ctx, smt311_disp_ctl);
		SMT311_WRITE(ctx, smt311_dad_ctl);
		SMT311_WRITE(ctx, smt311_global_param);
		SMT311_WRITE(ctx, smt311_rfd_ctl);
		SMT311_WRITE(ctx, smt311_pwr_ctl0);
		SMT311_WRITE(ctx, smt311_pwr_ctl1);
		SMT311_WRITE(ctx, smt311_pwr_ctl2);
		SMT311_WRITE(ctx, smt311_asg_ctl0);
		SMT311_WRITE(ctx, smt311_asg_ctl1);
		SMT311_WRITE(ctx, smt311_asg_ctl2);
		SMT311_WRITE(ctx, smt311_pgamma);
		SMT311_WRITE(ctx, smt311_ngamma);
		SMT311_WRITE(ctx, smt311_gout);
		SMT311_WRITE(ctx, smt311_bl_on);
		SMT311_WRITE(ctx, smt311_sleep_out);
		msleep(120);
	} else {
		profile = "MTP";
		SMT311_WRITE(ctx, smt311_otp_reload);
		SMT311_WRITE(ctx, smt311_bl_on);
		SMT311_WRITE(ctx, smt311_sleep_out);
		msleep(120);
	}

	SMT311_WRITE(ctx, smt311_passwd1_lock);
	SMT311_WRITE(ctx, smt311_passwd2_lock);
	SMT311_WRITE(ctx, smt311_display_on);
	dev_info(dev,
		 "SMT311_V128_PANEL_ID da=%02x db=%02x dc=%02x connected=%d profile=%s\n",
		 ctx->smt311_id[0], ctx->smt311_id[1], ctx->smt311_id[2],
		 ctx->smt311_connected, profile);
	return 0;
}

#undef SMT311_WRITE

static int s6d7aa0_lsl080al02_off(struct s6d7aa0 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;

	/* BL_CTL_OFF */
	mipi_dsi_dcs_write_seq(dsi, MCS_BL_CTL, 0x40, 0x00, 0x20);

	return 0;
}

static const struct drm_display_mode s6d7aa0_lsl080al02_mode = {
	.clock = (800 + 16 + 4 + 140) * (1280 + 8 + 4 + 4) * 58 / 1000,
	.hdisplay = 800,
	.hsync_start = 800 + 16,
	.hsync_end = 800 + 16 + 4,
	.htotal = 800 + 16 + 4 + 140,
	.vdisplay = 1280,
	.vsync_start = 1280 + 8,
	.vsync_end = 1280 + 8 + 4,
	.vtotal = 1280 + 8 + 4 + 4,
	.width_mm = 108,
	.height_mm = 173,
};

static const struct s6d7aa0_panel_desc s6d7aa0_lsl080al02_desc = {
	.panel_type = S6D7AA0_PANEL_LSL080AL02,
	.init_func = s6d7aa0_lsl080al02_init,
	.off_func = s6d7aa0_lsl080al02_off,
	.drm_mode = &s6d7aa0_lsl080al02_mode,
	.mode_flags = MIPI_DSI_MODE_VIDEO_NO_HFP,
	.bus_flags = 0,

	.has_backlight = false,
	.use_passwd3 = false,
};

/* Initialization code and structures for LSL080AL03 panel */

static int s6d7aa0_lsl080al03_init(struct s6d7aa0 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	usleep_range(20000, 25000);

	ret = s6d7aa0_lock(ctx, false);
	if (ret < 0) {
		dev_err(dev, "Failed to unlock registers: %d\n", ret);
		return ret;
	}

	if (ctx->desc->panel_type == S6D7AA0_PANEL_LSL080AL03) {
		mipi_dsi_dcs_write_seq(dsi, MCS_BL_CTL, 0xc7, 0x00, 0x29);
		mipi_dsi_dcs_write_seq(dsi, 0xbc, 0x01, 0x4e, 0xa0);
		mipi_dsi_dcs_write_seq(dsi, 0xfd, 0x16, 0x10, 0x11, 0x23,
				       0x09);
		mipi_dsi_dcs_write_seq(dsi, 0xfe, 0x00, 0x02, 0x03, 0x21,
				       0x80, 0x78);
	} else if (ctx->desc->panel_type == S6D7AA0_PANEL_LTL101AT01) {
		mipi_dsi_dcs_write_seq(dsi, MCS_BL_CTL, 0x40, 0x00, 0x08);
		mipi_dsi_dcs_write_seq(dsi, 0xbc, 0x01, 0x4e, 0x0b);
		mipi_dsi_dcs_write_seq(dsi, 0xfd, 0x16, 0x10, 0x11, 0x23,
				       0x09);
		mipi_dsi_dcs_write_seq(dsi, 0xfe, 0x00, 0x02, 0x03, 0x21,
				       0x80, 0x68);
	}

	mipi_dsi_dcs_write_seq(dsi, 0xb3, 0x51);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x24);
	mipi_dsi_dcs_write_seq(dsi, 0xf2, 0x02, 0x08, 0x08);

	usleep_range(10000, 11000);

	mipi_dsi_dcs_write_seq(dsi, 0xc0, 0x80, 0x80, 0x30);
	mipi_dsi_dcs_write_seq(dsi, 0xcd,
			       0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
			       0x2e, 0x2e, 0x2e, 0x2e, 0x2e);
	mipi_dsi_dcs_write_seq(dsi, 0xce,
			       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xc1, 0x03);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}

	ret = s6d7aa0_lock(ctx, true);
	if (ret < 0) {
		dev_err(dev, "Failed to lock registers: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}

	return 0;
}

static int s6d7aa0_lsl080al03_off(struct s6d7aa0 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;

	mipi_dsi_dcs_write_seq(dsi, 0x22, 0x00);

	return 0;
}

static const struct drm_display_mode s6d7aa0_lsl080al03_mode = {
	.clock = (768 + 18 + 16 + 126) * (1024 + 8 + 2 + 6) * 60 / 1000,
	.hdisplay = 768,
	.hsync_start = 768 + 18,
	.hsync_end = 768 + 18 + 16,
	.htotal = 768 + 18 + 16 + 126,
	.vdisplay = 1024,
	.vsync_start = 1024 + 8,
	.vsync_end = 1024 + 8 + 2,
	.vtotal = 1024 + 8 + 2 + 6,
	.width_mm = 122,
	.height_mm = 163,
};

static const struct s6d7aa0_panel_desc s6d7aa0_lsl080al03_desc = {
	.panel_type = S6D7AA0_PANEL_LSL080AL03,
	.init_func = s6d7aa0_lsl080al03_init,
	.off_func = s6d7aa0_lsl080al03_off,
	.drm_mode = &s6d7aa0_lsl080al03_mode,
	.mode_flags = MIPI_DSI_MODE_NO_EOT_PACKET,
	.bus_flags = 0,

	.has_backlight = true,
	.use_passwd3 = true,
};

/* Initialization structures for LTL101AT01 panel */

static const struct drm_display_mode s6d7aa0_ltl101at01_mode = {
	.clock = (768 + 96 + 16 + 184) * (1024 + 8 + 2 + 6) * 60 / 1000,
	.hdisplay = 768,
	.hsync_start = 768 + 96,
	.hsync_end = 768 + 96 + 16,
	.htotal = 768 + 96 + 16 + 184,
	.vdisplay = 1024,
	.vsync_start = 1024 + 8,
	.vsync_end = 1024 + 8 + 2,
	.vtotal = 1024 + 8 + 2 + 6,
	.width_mm = 148,
	.height_mm = 197,
};

static const struct s6d7aa0_panel_desc s6d7aa0_ltl101at01_desc = {
	.panel_type = S6D7AA0_PANEL_LTL101AT01,
	.init_func = s6d7aa0_lsl080al03_init, /* Similar init to LSL080AL03 */
	.off_func = s6d7aa0_lsl080al03_off,
	.drm_mode = &s6d7aa0_ltl101at01_mode,
	.mode_flags = MIPI_DSI_MODE_NO_EOT_PACKET,
	.bus_flags = 0,

	.has_backlight = true,
	.use_passwd3 = true,
};

static int s6d7aa0_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	struct s6d7aa0 *ctx;

	ctx = container_of(panel, struct s6d7aa0, panel);
	if (!ctx)
		return -EINVAL;

	mode = drm_mode_duplicate(connector->dev, ctx->desc->drm_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	connector->display_info.bus_flags = ctx->desc->bus_flags;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs s6d7aa0_panel_funcs = {
	.disable = s6d7aa0_disable,
	.unprepare = s6d7aa0_unprepare,
	.prepare = s6d7aa0_prepare,
	.enable = s6d7aa0_enable,
	.get_modes = s6d7aa0_get_modes,
};

static int s6d7aa0_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct s6d7aa0 *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct s6d7aa0, panel,
				   &s6d7aa0_panel_funcs, DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->desc = of_device_get_match_data(dev);
	if (!ctx->desc)
		return -ENODEV;

	ctx->supplies[0].supply = "power";
	ctx->supplies[1].supply = "vmipi";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
					      ctx->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;

#ifdef CONFIG_ARM
	if (ctx->desc->panel_type == S6D7AA0_PANEL_LSL080AL02 &&
	    of_machine_is_compatible("samsung,tab3")) {
		/* The downstream driver used system_rev >= 2. The DT port must not
		 * depend on legacy ATAG system_rev, which remains zero here. The
		 * board DTS exposes error-gpios only when ERR_FG exists. */
		ctx->smt311_error_gpio = devm_gpiod_get_optional(dev, "error", GPIOD_IN);
		if (IS_ERR(ctx->smt311_error_gpio))
			return dev_err_probe(dev, PTR_ERR(ctx->smt311_error_gpio),
					     "Failed to get stock ERR_FG gpio\n");
		if (ctx->smt311_error_gpio) {
			ret = s6d7aa0_smt311_set_error_bias(ctx, PIN_CONFIG_BIAS_DISABLE,
							    "SMT311_V130_ERR_FG_BIAS_NONE");
			if (ret)
				return dev_err_probe(dev, ret,
						     "Failed to set stock ERR_FG no-pull state\n");
			atomic_set(&ctx->smt311_error_edge_count, 0);
			INIT_DELAYED_WORK(&ctx->smt311_error_work,
					  s6d7aa0_smt311_error_work);
			ctx->smt311_error_irq = gpiod_to_irq(ctx->smt311_error_gpio);
			if (ctx->smt311_error_irq < 0)
				return dev_err_probe(dev, ctx->smt311_error_irq,
						     "Failed to map stock ERR_FG IRQ\n");
			ret = devm_request_threaded_irq(dev, ctx->smt311_error_irq,
						NULL, s6d7aa0_smt311_error_irq_thread,
						IRQF_TRIGGER_FALLING | IRQF_ONESHOT |
						IRQF_NO_AUTOEN,
						"s6d7aa0-err-fg", ctx);
			if (ret)
				return dev_err_probe(dev, ret,
						     "Failed to request stock ERR_FG IRQ\n");
			ctx->smt311_error_irq_enabled = false;
			dev_info(dev,
				 "SMT311_V130_ERR_FG_MONITOR_READY irq=%d active_low=1 filter_samples=10 source=devicetree legacy_system_rev=%x\n",
				 ctx->smt311_error_irq, system_rev);
		} else {
			dev_info(dev,
				 "SMT311_V131_ERR_FG_DT_NOT_DESCRIBED legacy_system_rev=%x\n",
				 system_rev);
		}
	}
#endif
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST
		| ctx->desc->mode_flags;

	if (ctx->desc->panel_type == S6D7AA0_PANEL_LSL080AL02)
		dev_info(dev,
			 "SMT311_V126_PANEL_PROFILE pclk_khz=%d refresh_hz=58 lanes=%u format=%d mode_flags=%08lx bus_flags=%08x\n",
			 ctx->desc->drm_mode->clock, dsi->lanes, (int)dsi->format,
			 dsi->mode_flags, ctx->desc->bus_flags);

	ctx->panel.prepare_prev_first = true;

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");

	/* Use DSI-based backlight as fallback if available */
	if (ctx->desc->has_backlight && !ctx->panel.backlight) {
		ctx->panel.backlight = s6d7aa0_create_backlight(dsi);
		if (IS_ERR(ctx->panel.backlight))
			return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
					     "Failed to create backlight\n");
	}

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	if (ctx->desc->panel_type == S6D7AA0_PANEL_LSL080AL02 &&
	    of_machine_is_compatible("samsung,tab3")) {
		ret = samsung_dsim_smt311_register_panel(dsi,
			s6d7aa0_smt311_reset_after_link,
			s6d7aa0_smt311_powered, ctx);
		if (ret) {
			mipi_dsi_detach(dsi);
			drm_panel_remove(&ctx->panel);
			return dev_err_probe(dev, ret,
				"Failed to register stock SM-T311 panel sequencing\n");
		}
	}

	return 0;
}

static void s6d7aa0_remove(struct mipi_dsi_device *dsi)
{
	struct s6d7aa0 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	s6d7aa0_smt311_stop_error_monitor(ctx);
	if (ctx->desc->panel_type == S6D7AA0_PANEL_LSL080AL02 &&
	    of_machine_is_compatible("samsung,tab3"))
		samsung_dsim_smt311_unregister_panel(dsi, ctx);

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id s6d7aa0_of_match[] = {
	{
		.compatible = "samsung,lsl080al02",
		.data = &s6d7aa0_lsl080al02_desc
	},
	{
		.compatible = "samsung,lsl080al03",
		.data = &s6d7aa0_lsl080al03_desc
	},
	{
		.compatible = "samsung,ltl101at01",
		.data = &s6d7aa0_ltl101at01_desc
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, s6d7aa0_of_match);

static struct mipi_dsi_driver s6d7aa0_driver = {
	.probe = s6d7aa0_probe,
	.remove = s6d7aa0_remove,
	.driver = {
		.name = "panel-samsung-s6d7aa0",
		.of_match_table = s6d7aa0_of_match,
	},
};
module_mipi_dsi_driver(s6d7aa0_driver);

MODULE_AUTHOR("Artur Weber <aweber.kernel@gmail.com>");
MODULE_DESCRIPTION("Samsung S6D7AA0 MIPI-DSI LCD controller driver");
MODULE_LICENSE("GPL");
