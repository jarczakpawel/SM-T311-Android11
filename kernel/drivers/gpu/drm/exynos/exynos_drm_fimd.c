// SPDX-License-Identifier: GPL-2.0-or-later
/* exynos_drm_fimd.c
 *
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Authors:
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Inki Dae <inki.dae@samsung.com>
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/moduleparam.h>
#include <linux/spinlock.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <video/of_display_timing.h>
#include <video/of_videomode.h>
#include <video/samsung_fimd.h>

#include <drm/drm_blend.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_print.h>
#include <drm/drm_vblank.h>
#include <drm/exynos_drm.h>

#include "exynos_drm_crtc.h"
#include "exynos_drm_drv.h"
#include "exynos4_mdnie.h"
#include "exynos_drm_fb.h"
#include "exynos_drm_plane.h"

/*
 * FIMD stands for Fully Interactive Mobile Display and
 * as a display controller, it transfers contents drawn on memory
 * to a LCD Panel through Display Interfaces such as RGB or
 * CPU Interface.
 */

#define MIN_FB_WIDTH_FOR_16WORD_BURST 128

/* position control register for hardware window 0, 2 ~ 4.*/
#define VIDOSD_A(win)		(VIDOSD_BASE + 0x00 + (win) * 16)
#define VIDOSD_B(win)		(VIDOSD_BASE + 0x04 + (win) * 16)
/*
 * size control register for hardware windows 0 and alpha control register
 * for hardware windows 1 ~ 4
 */
#define VIDOSD_C(win)		(VIDOSD_BASE + 0x08 + (win) * 16)
/* size control register for hardware windows 1 ~ 2. */
#define VIDOSD_D(win)		(VIDOSD_BASE + 0x0C + (win) * 16)

#define VIDWnALPHA0(win)	(VIDW_ALPHA + 0x00 + (win) * 8)
#define VIDWnALPHA1(win)	(VIDW_ALPHA + 0x04 + (win) * 8)

#define VIDWx_BUF_START(win, buf)	(VIDW_BUF_START(buf) + (win) * 8)
#define VIDWx_BUF_START_S(win, buf)	(VIDW_BUF_START_S(buf) + (win) * 8)
#define VIDWx_BUF_END(win, buf)		(VIDW_BUF_END(buf) + (win) * 8)
#define VIDWx_BUF_SIZE(win, buf)	(VIDW_BUF_SIZE(buf) + (win) * 4)

/* color key control register for hardware window 1 ~ 4. */
#define WKEYCON0_BASE(x)		((WKEYCON0 + 0x140) + ((x - 1) * 8))
/* color key value register for hardware window 1 ~ 4. */
#define WKEYCON1_BASE(x)		((WKEYCON1 + 0x140) + ((x - 1) * 8))

/* I80 trigger control register */
#define TRIGCON				0x1A4
#define TRGMODE_ENABLE			(1 << 0)
#define SWTRGCMD_ENABLE			(1 << 1)
/* Exynos3250, 3472, 5260 5410, 5420 and 5422 only supported. */
#define HWTRGEN_ENABLE			(1 << 3)
#define HWTRGMASK_ENABLE		(1 << 4)
/* Exynos3250, 3472, 5260, 5420 and 5422 only supported. */
#define HWTRIGEN_PER_ENABLE		(1 << 31)

/* display mode change control register except exynos4 */
#define VIDOUT_CON			0x000
#define VIDOUT_CON_F_I80_LDI0		(0x2 << 8)

/* I80 interface control for main LDI register */
#define I80IFCONFAx(x)			(0x1B0 + (x) * 4)
#define I80IFCONFBx(x)			(0x1B8 + (x) * 4)
#define LCD_CS_SETUP(x)			((x) << 16)
#define LCD_WR_SETUP(x)			((x) << 12)
#define LCD_WR_ACTIVE(x)		((x) << 8)
#define LCD_WR_HOLD(x)			((x) << 4)
#define I80IFEN_ENABLE			(1 << 0)

/* FIMD has totally five hardware windows. */
#define WINDOWS_NR	5

/*
 * One-shot diagnostic on the dedicated EXYNOS4 FIFO IRQ, off by default.
 * Do NOT turn on for production until verified hardware recovery is ready.
 * Enable only at boot (read-only module parameter). At the first FIFO EMPTY
 * condition we mask the source and record exactly one diagnostic message.
 */
static bool fimd_fifo_one_shot;
module_param_named(fimd_fifo_one_shot, fimd_fifo_one_shot, bool, 0444);
MODULE_PARM_DESC(fimd_fifo_one_shot,
	"One-shot EXYNOS4 LCD FIFO-empty diagnostic (default disabled)");
#define FIMD_FIFO_IRQ_ENABLE	BIT(1)

/* HW trigger flag on i80 panel. */
#define I80_HW_TRG     (1 << 1)

struct fimd_driver_data {
	unsigned int timing_base;
	unsigned int lcdblk_offset;
	unsigned int lcdblk_vt_shift;
	unsigned int lcdblk_bypass_shift;
	unsigned int lcdblk_mic_bypass_shift;
	unsigned int trg_type;

	unsigned int has_shadowcon:1;
	unsigned int has_clksel:1;
	unsigned int has_limited_fmt:1;
	unsigned int has_vidoutcon:1;
	unsigned int has_vtsel:1;
	unsigned int has_mic_bypass:1;
	unsigned int has_dp_clk:1;
	unsigned int has_hw_trigger:1;
	unsigned int has_trigger_per_te:1;
	unsigned int has_bgr_support:1;
};

static struct fimd_driver_data s3c64xx_fimd_driver_data = {
	.timing_base = 0x0,
	.has_clksel = 1,
	.has_limited_fmt = 1,
};

static struct fimd_driver_data s5pv210_fimd_driver_data = {
	.timing_base = 0x0,
	.has_shadowcon = 1,
	.has_clksel = 1,
};

static struct fimd_driver_data exynos3_fimd_driver_data = {
	.timing_base = 0x20000,
	.lcdblk_offset = 0x210,
	.lcdblk_bypass_shift = 1,
	.has_shadowcon = 1,
	.has_vidoutcon = 1,
};

static struct fimd_driver_data exynos4_fimd_driver_data = {
	.timing_base = 0x0,
	.lcdblk_offset = 0x210,
	.lcdblk_vt_shift = 10,
	.lcdblk_bypass_shift = 1,
	.has_shadowcon = 1,
	.has_vtsel = 1,
	.has_bgr_support = 1,
};

static struct fimd_driver_data exynos5_fimd_driver_data = {
	.timing_base = 0x20000,
	.lcdblk_offset = 0x214,
	.lcdblk_vt_shift = 24,
	.lcdblk_bypass_shift = 15,
	.has_shadowcon = 1,
	.has_vidoutcon = 1,
	.has_vtsel = 1,
	.has_dp_clk = 1,
	.has_bgr_support = 1,
};

static struct fimd_driver_data exynos5420_fimd_driver_data = {
	.timing_base = 0x20000,
	.lcdblk_offset = 0x214,
	.lcdblk_vt_shift = 24,
	.lcdblk_bypass_shift = 15,
	.lcdblk_mic_bypass_shift = 11,
	.has_shadowcon = 1,
	.has_vidoutcon = 1,
	.has_vtsel = 1,
	.has_mic_bypass = 1,
	.has_dp_clk = 1,
	.has_bgr_support = 1,
};

struct fimd_context;
static bool fimd_is_tab3(struct fimd_context *ctx);

struct fimd_context {
	struct device			*dev;
	struct drm_device		*drm_dev;
	void				*dma_priv;
	struct exynos_drm_crtc		*crtc;
	struct exynos_drm_plane		planes[WINDOWS_NR];
	struct exynos_drm_plane_config	configs[WINDOWS_NR];
	struct clk			*bus_clk;
	struct clk			*lcd_clk;
	void __iomem			*regs;
	struct regmap			*sysreg;
	struct exynos4_mdnie		*mdnie;
	unsigned long			irq_flags;
	u32				vidcon0;
	u32				vidcon1;
	u32				vidout_con;
	u32				i80ifcon;
	bool				i80_if;
	bool				suspended;
	bool				dp_clk_enabled;
	bool				smt311_pipeline_deferred;
	bool				smt311_pipeline_started;
	u32				smt311_pipeline_seq;
	unsigned int			primary_win;
	wait_queue_head_t		wait_vsync_queue;
	atomic_t			wait_vsync_event;
	atomic_t			win_updated;
	atomic_t			triggering;
	u32				clkdiv;

	const struct fimd_driver_data *driver_data;
	struct drm_encoder *encoder;
	struct exynos_drm_clk		dp_clk;
	/* Kept at the end: all preceding fields retain their offsets. */
	spinlock_t			fifo_irq_lock;
	bool				fifo_diag_registered;
	bool				fifo_diag_latched;
	atomic_t			fifo_diag_active_vblanks;
	u32				fifo_diag_window_mask;
};

static const struct of_device_id fimd_driver_dt_match[] = {
	{ .compatible = "samsung,s3c6400-fimd",
	  .data = &s3c64xx_fimd_driver_data },
	{ .compatible = "samsung,s5pv210-fimd",
	  .data = &s5pv210_fimd_driver_data },
	{ .compatible = "samsung,exynos3250-fimd",
	  .data = &exynos3_fimd_driver_data },
	{ .compatible = "samsung,exynos4210-fimd",
	  .data = &exynos4_fimd_driver_data },
	{ .compatible = "samsung,exynos5250-fimd",
	  .data = &exynos5_fimd_driver_data },
	{ .compatible = "samsung,exynos5420-fimd",
	  .data = &exynos5420_fimd_driver_data },
	{},
};
MODULE_DEVICE_TABLE(of, fimd_driver_dt_match);

static const enum drm_plane_type fimd_win_types[WINDOWS_NR] = {
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_OVERLAY,
	DRM_PLANE_TYPE_OVERLAY,
	DRM_PLANE_TYPE_OVERLAY,
	DRM_PLANE_TYPE_CURSOR,
};

static const uint32_t fimd_formats[] = {
	DRM_FORMAT_C8,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
};

static const uint32_t fimd_extended_formats[] = {
	DRM_FORMAT_C8,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_XBGR1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
};

/* Window 0 has no A888 mode on Exynos4 FIMD. */
static const u32 fimd_exynos4_win0_formats[] = {
	DRM_FORMAT_C8,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_XBGR1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
};

static const unsigned int capabilities[WINDOWS_NR] = {
	0,
	EXYNOS_DRM_PLANE_CAP_WIN_BLEND | EXYNOS_DRM_PLANE_CAP_PIX_BLEND,
	EXYNOS_DRM_PLANE_CAP_WIN_BLEND | EXYNOS_DRM_PLANE_CAP_PIX_BLEND,
	EXYNOS_DRM_PLANE_CAP_WIN_BLEND | EXYNOS_DRM_PLANE_CAP_PIX_BLEND,
	EXYNOS_DRM_PLANE_CAP_WIN_BLEND | EXYNOS_DRM_PLANE_CAP_PIX_BLEND,
};

static inline void fimd_set_bits(struct fimd_context *ctx, u32 reg, u32 mask,
				 u32 val)
{
	val = (val & mask) | (readl(ctx->regs + reg) & ~mask);
	writel(val, ctx->regs + reg);
}

/*
 * VIDINTCON0 FIFOSEL contains one bit for each FIMD source window (the bit
 * encodings for WIN2-4 are not sequential with WIN0-1). Select only the
 * DMA scanout channels which are both enabled in WINCON and SHADOWCON.
 *
 * On SM-T311, WIN2 is the opaque main framebuffer and WIN3 is the
 * additional hardware HWC overlay in mixed CLIENT+DEVICE compositions.
 * Selecting only WIN2 makes a WIN3 underflow invisible; selecting disabled
 * windows risks a spurious EMPTY interrupt. This is diagnostic opt-in only.
 */
static u32 fimd_fifo_active_window_selector(struct fimd_context *ctx)
{
	static const u32 selectors[WINDOWS_NR] = {
		VIDINTCON0_FIFIOSEL_WINDOW0,
		VIDINTCON0_FIFIOSEL_WINDOW1,
		VIDINTCON0_FIFIOSEL_WINDOW2,
		VIDINTCON0_FIFIOSEL_WINDOW3,
		VIDINTCON0_FIFIOSEL_WINDOW4,
	};
	u32 shadow, active = 0;
	unsigned int i;

	if (!ctx->driver_data->has_shadowcon)
		return 0;

	shadow = readl(ctx->regs + SHADOWCON);
	for (i = 0; i < WINDOWS_NR; i++) {
		if (!(shadow & SHADOWCON_CHx_ENABLE(i)))
			continue;
		if (!(readl(ctx->regs + WINCON(i)) & WINCONx_ENWIN))
			continue;
		active |= selectors[i];
	}
	return active;
}

static void fimd_fifo_arm_after_vblank(struct fimd_context *ctx)
{
	unsigned long flags;
	u32 val, windows;

	if (!ctx->fifo_diag_registered || ctx->fifo_diag_latched ||
	    !ctx->smt311_pipeline_started || ctx->suspended)
		return;

	spin_lock_irqsave(&ctx->fifo_irq_lock, flags);
	val = readl(ctx->regs + VIDINTCON0);
	if (!ctx->fifo_diag_latched && (val & VIDINTCON0_INT_ENABLE)) {
		windows = fimd_fifo_active_window_selector(ctx);
		if (windows) {
			/* Clear stale status; never arm an idle DMA FIFO. */
			writel(VIDINTCON1_INT_FIFO, ctx->regs + VIDINTCON1);
			val &= ~(VIDINTCON0_FIFIOSEL_MASK |
				 VIDINTCON0_FIFOLEVEL_MASK);
			val |= windows | VIDINTCON0_FIFOLEVEL_EMPTY |
				FIMD_FIFO_IRQ_ENABLE;
			writel(val, ctx->regs + VIDINTCON0);
			ctx->fifo_diag_window_mask = windows;
		}
	}
	spin_unlock_irqrestore(&ctx->fifo_irq_lock, flags);
}

static int fimd_enable_vblank(struct exynos_drm_crtc *crtc)
{
	struct fimd_context *ctx = crtc->ctx;
	unsigned long flags;
	u32 val;

	if (ctx->suspended)
		return -EPERM;

	spin_lock_irqsave(&ctx->fifo_irq_lock, flags);
	if (!test_and_set_bit(0, &ctx->irq_flags)) {
		val = readl(ctx->regs + VIDINTCON0);

		val |= VIDINTCON0_INT_ENABLE;

		if (ctx->i80_if) {
			val |= VIDINTCON0_INT_I80IFDONE;
			val |= VIDINTCON0_INT_SYSMAINCON;
			val &= ~VIDINTCON0_INT_SYSSUBCON;
		} else {
			val |= VIDINTCON0_INT_FRAME;

			val &= ~VIDINTCON0_FRAMESEL0_MASK;
			val |= VIDINTCON0_FRAMESEL0_FRONTPORCH;
			val &= ~VIDINTCON0_FRAMESEL1_MASK;
			val |= VIDINTCON0_FRAMESEL1_NONE;
		}

		writel(val, ctx->regs + VIDINTCON0);
	}
	spin_unlock_irqrestore(&ctx->fifo_irq_lock, flags);

	return 0;
}

static void fimd_disable_vblank(struct exynos_drm_crtc *crtc)
{
	struct fimd_context *ctx = crtc->ctx;
	unsigned long flags;
	u32 val;

	if (ctx->suspended)
		return;

	spin_lock_irqsave(&ctx->fifo_irq_lock, flags);
	if (test_and_clear_bit(0, &ctx->irq_flags)) {
		val = readl(ctx->regs + VIDINTCON0);

		val &= ~VIDINTCON0_INT_ENABLE;

		if (ctx->i80_if) {
			val &= ~VIDINTCON0_INT_I80IFDONE;
			val &= ~VIDINTCON0_INT_SYSMAINCON;
			val &= ~VIDINTCON0_INT_SYSSUBCON;
		} else
			val &= ~VIDINTCON0_INT_FRAME;

		/* Off by default: leave legacy FIFO enable bits as-is. */
		if (unlikely(ctx->fifo_diag_registered))
			val &= ~FIMD_FIFO_IRQ_ENABLE;
		writel(val, ctx->regs + VIDINTCON0);
	}
	spin_unlock_irqrestore(&ctx->fifo_irq_lock, flags);
}

static void fimd_wait_for_vblank(struct exynos_drm_crtc *crtc)
{
	struct fimd_context *ctx = crtc->ctx;

	if (ctx->suspended)
		return;

	atomic_set(&ctx->wait_vsync_event, 1);

	/*
	 * wait for FIMD to signal VSYNC interrupt or return after
	 * timeout which is set to 50ms (refresh rate of 20).
	 */
	if (!wait_event_timeout(ctx->wait_vsync_queue,
				!atomic_read(&ctx->wait_vsync_event),
				HZ/20))
		DRM_DEV_DEBUG_KMS(ctx->dev, "vblank wait timed out.\n");
}

static void fimd_enable_video_output(struct fimd_context *ctx, unsigned int win,
					bool enable)
{
	u32 val = readl(ctx->regs + WINCON(win));

	if (enable)
		val |= WINCONx_ENWIN;
	else
		val &= ~WINCONx_ENWIN;

	writel(val, ctx->regs + WINCON(win));
}

static void fimd_enable_shadow_channel_path(struct fimd_context *ctx,
						unsigned int win,
						bool enable)
{
	u32 val = readl(ctx->regs + SHADOWCON);

	if (enable)
		val |= SHADOWCON_CHx_ENABLE(win);
	else
		val &= ~SHADOWCON_CHx_ENABLE(win);

	writel(val, ctx->regs + SHADOWCON);
}

static void fimd_smt311_program_rgb_bypass(struct fimd_context *ctx)
{
	u32 val = readl(ctx->regs + VIDCON2);

	/* Establish a deterministic RGB baseline before selecting the final path. */
	val &= ~(VIDCON2_EN601 | VIDCON2_WB | VIDCON2_TVFMTSEL_SW |
		 VIDCON2_TVFMTSEL1_MASK | VIDCON2_ORGYCbCr |
		 VIDCON2_YUVORDCrCb);
	writel(val, ctx->regs + VIDCON2);
	writel(DUALRGB_BYPASS_SINGLE, ctx->regs + DUALRGB);
}

static void fimd_smt311_reset_handoff(struct fimd_context *ctx)
{
	unsigned int win;

	DRM_DEV_INFO(ctx->dev,
		     "SMT311_V132_FIMD_HANDOFF_BEFORE_GLOBAL vidcon0=%08x vidcon2=%08x dualrgb=%08x shadowcon=%08x\n",
		     readl(ctx->regs + VIDCON0), readl(ctx->regs + VIDCON2),
		     readl(ctx->regs + DUALRGB), readl(ctx->regs + SHADOWCON));
	DRM_DEV_INFO(ctx->dev,
		     "SMT311_V132_FIMD_HANDOFF_BEFORE_WINDOWS wincon0=%08x wincon3=%08x winmap0=%08x winmap3=%08x\n",
		     readl(ctx->regs + WINCON(0)), readl(ctx->regs + WINCON(3)),
		     readl(ctx->regs + WINxMAP(0)), readl(ctx->regs + WINxMAP(3)));

	/* No scanout is active here. Remove every bootloader-owned DMA state. */
	writel(0, ctx->regs + VIDCON0);
	for (win = 0; win < WINDOWS_NR; win++) {
		writel(0, ctx->regs + WINCON(win));
		writel(0, ctx->regs + WINxMAP(win));
		writel(0, ctx->regs + VIDWx_BUF_START(win, 0));
		writel(0, ctx->regs + VIDWx_BUF_END(win, 0));
		writel(0, ctx->regs + VIDWx_BUF_SIZE(win, 0));
	}
	if (ctx->driver_data->has_shadowcon)
		writel(0, ctx->regs + SHADOWCON);

	fimd_smt311_program_rgb_bypass(ctx);
	readl(ctx->regs + DUALRGB);

	DRM_DEV_INFO(ctx->dev,
		     "SMT311_V132_FIMD_HANDOFF_AFTER_GLOBAL vidcon0=%08x vidcon2=%08x dualrgb=%08x shadowcon=%08x\n",
		     readl(ctx->regs + VIDCON0), readl(ctx->regs + VIDCON2),
		     readl(ctx->regs + DUALRGB), readl(ctx->regs + SHADOWCON));
	DRM_DEV_INFO(ctx->dev,
		     "SMT311_V132_FIMD_HANDOFF_AFTER_WINDOWS wincon0=%08x wincon3=%08x winmap0=%08x winmap3=%08x\n",
		     readl(ctx->regs + WINCON(0)), readl(ctx->regs + WINCON(3)),
		     readl(ctx->regs + WINxMAP(0)), readl(ctx->regs + WINxMAP(3)));
}

static int fimd_clear_channels(struct exynos_drm_crtc *crtc,
			       bool reset_smt311_handoff)
{
	struct fimd_context *ctx = crtc->ctx;
	unsigned int win, ch_enabled = 0;
	int ret;

	/* Hardware is in unknown state, so ensure it gets enabled properly */
	ret = pm_runtime_resume_and_get(ctx->dev);
	if (ret < 0) {
		dev_err(ctx->dev, "failed to enable FIMD device.\n");
		return ret;
	}

	/*
	 * Runtime resume already enables bus_clk and lcd_clk. Do not
	 * acquire a second pair of clock references here; clock lifetime
	 * is owned by the runtime PM callbacks.
	 */

	/* Check if any channel is enabled. */
	for (win = 0; win < WINDOWS_NR; win++) {
		u32 val = readl(ctx->regs + WINCON(win));

		if (val & WINCONx_ENWIN) {
			fimd_enable_video_output(ctx, win, false);

			if (ctx->driver_data->has_shadowcon)
				fimd_enable_shadow_channel_path(ctx, win,
								false);

			ch_enabled = 1;
		}
	}

	/* Wait for vsync, as disable channel takes effect at next vsync */
	if (ch_enabled) {
		ctx->suspended = false;

		fimd_enable_vblank(ctx->crtc);
		fimd_wait_for_vblank(ctx->crtc);
		fimd_disable_vblank(ctx->crtc);

		ctx->suspended = true;
	}

	if (reset_smt311_handoff)
		fimd_smt311_reset_handoff(ctx);

	pm_runtime_put(ctx->dev);

	return 0;
}


static int fimd_atomic_check(struct exynos_drm_crtc *crtc,
		struct drm_crtc_state *state)
{
	struct drm_display_mode *mode = &state->adjusted_mode;
	struct fimd_context *ctx = crtc->ctx;
	unsigned long ideal_clk, lcd_rate;
	u32 clkdiv;

	if (mode->clock == 0) {
		DRM_DEV_ERROR(ctx->dev, "Mode has zero clock value.\n");
		return -EINVAL;
	}

	ideal_clk = mode->clock * 1000;

	if (ctx->i80_if) {
		/*
		 * The frame done interrupt should be occurred prior to the
		 * next TE signal.
		 */
		ideal_clk *= 2;
	}

	lcd_rate = clk_get_rate(ctx->lcd_clk);
	if (2 * lcd_rate < ideal_clk) {
		DRM_DEV_ERROR(ctx->dev,
			      "sclk_fimd clock too low(%lu) for requested pixel clock(%lu)\n",
			      lcd_rate, ideal_clk);
		return -EINVAL;
	}

	/* Find the clock divider value that gets us closest to ideal_clk */
	clkdiv = DIV_ROUND_CLOSEST(lcd_rate, ideal_clk);
	if (clkdiv >= 0x200) {
		DRM_DEV_ERROR(ctx->dev, "requested pixel clock(%lu) too low\n",
			      ideal_clk);
		return -EINVAL;
	}

	/* Test-only atomic checks must not change the programmed divider. */

	return 0;
}

static void fimd_setup_trigger(struct fimd_context *ctx)
{
	void __iomem *timing_base = ctx->regs + ctx->driver_data->timing_base;
	u32 trg_type = ctx->driver_data->trg_type;
	u32 val = readl(timing_base + TRIGCON);

	val &= ~(TRGMODE_ENABLE);

	if (trg_type == I80_HW_TRG) {
		if (ctx->driver_data->has_hw_trigger)
			val |= HWTRGEN_ENABLE | HWTRGMASK_ENABLE;
		if (ctx->driver_data->has_trigger_per_te)
			val |= HWTRIGEN_PER_ENABLE;
	} else {
		val |= TRGMODE_ENABLE;
	}

	writel(val, timing_base + TRIGCON);
}

static void fimd_commit(struct exynos_drm_crtc *crtc)
{
	struct fimd_context *ctx = crtc->ctx;
	struct drm_display_mode *mode = &crtc->base.state->adjusted_mode;
	const struct fimd_driver_data *driver_data = ctx->driver_data;
	void __iomem *timing_base = ctx->regs + driver_data->timing_base;
	unsigned long ideal_clk, lcd_rate;
	u32 val, clkdiv;

	if (ctx->suspended)
		return;

	/* nothing to do if we haven't set the mode yet */
	if (mode->htotal == 0 || mode->vtotal == 0)
		return;

	/*
	 * A TEST_ONLY check may have calculated another mode's divider.
	 * Revalidate against the actual LCD rate before writing any timing
	 * register, but do not publish the divider until we commit.
	 */
	if (!mode->clock) {
		DRM_DEV_ERROR(ctx->dev, "Cannot commit zero pixel clock.\n");
		return;
	}
	ideal_clk = (unsigned long)mode->clock * 1000;
	if (ctx->i80_if)
		ideal_clk *= 2;
	lcd_rate = clk_get_rate(ctx->lcd_clk);
	if (2 * lcd_rate < ideal_clk) {
		DRM_DEV_ERROR(ctx->dev, "LCD rate too low during commit.\n");
		return;
	}
	clkdiv = DIV_ROUND_CLOSEST(lcd_rate, ideal_clk);
	if (clkdiv >= 0x200) {
		DRM_DEV_ERROR(ctx->dev, "LCD divider too large during commit.\n");
		return;
	}

	if (ctx->i80_if) {
		val = ctx->i80ifcon | I80IFEN_ENABLE;
		writel(val, timing_base + I80IFCONFAx(0));

		/* disable auto frame rate */
		writel(0, timing_base + I80IFCONFBx(0));

		/* set video type selection to I80 interface */
		if (driver_data->has_vtsel && ctx->sysreg &&
				regmap_update_bits(ctx->sysreg,
					driver_data->lcdblk_offset,
					0x3 << driver_data->lcdblk_vt_shift,
					0x1 << driver_data->lcdblk_vt_shift)) {
			DRM_DEV_ERROR(ctx->dev,
				      "Failed to update sysreg for I80 i/f.\n");
			return;
		}
	} else {
		int vsync_len, vbpd, vfpd, hsync_len, hbpd, hfpd;
		u32 vidcon1;

		/* setup polarity values */
		vidcon1 = ctx->vidcon1;
		if (fimd_is_tab3(ctx))
			/* Stock s3cfb_set_polarity() leaves VCLK control at 0. */
			vidcon1 &= ~VIDCON1_VCLK_MASK;
		if (mode->flags & DRM_MODE_FLAG_NVSYNC)
			vidcon1 |= VIDCON1_INV_VSYNC;
		if (mode->flags & DRM_MODE_FLAG_NHSYNC)
			vidcon1 |= VIDCON1_INV_HSYNC;
		writel(vidcon1, ctx->regs + driver_data->timing_base + VIDCON1);

		/* setup vertical timing values. */
		vsync_len = mode->crtc_vsync_end - mode->crtc_vsync_start;
		vbpd = mode->crtc_vtotal - mode->crtc_vsync_end;
		vfpd = mode->crtc_vsync_start - mode->crtc_vdisplay;

		val = VIDTCON0_VBPD(vbpd - 1) |
			VIDTCON0_VFPD(vfpd - 1) |
			VIDTCON0_VSPW(vsync_len - 1);
		writel(val, ctx->regs + driver_data->timing_base + VIDTCON0);

		/* setup horizontal timing values.  */
		hsync_len = mode->crtc_hsync_end - mode->crtc_hsync_start;
		hbpd = mode->crtc_htotal - mode->crtc_hsync_end;
		hfpd = mode->crtc_hsync_start - mode->crtc_hdisplay;

		val = VIDTCON1_HBPD(hbpd - 1) |
			VIDTCON1_HFPD(hfpd - 1) |
			VIDTCON1_HSPW(hsync_len - 1);
		writel(val, ctx->regs + driver_data->timing_base + VIDTCON1);
	}

	/* The bootloader may leave LCD_BLK_CFG in an I80 routing state. */
	if (ctx->smt311_pipeline_deferred && driver_data->has_vtsel &&
	    ctx->sysreg && regmap_update_bits(ctx->sysreg,
				  driver_data->lcdblk_offset,
				  0x3 << driver_data->lcdblk_vt_shift, 0)) {
		DRM_DEV_ERROR(ctx->dev,
			      "Failed to select RGB video path in sysreg.\n");
		return;
	}

	if (driver_data->has_vidoutcon)
		writel(ctx->vidout_con, timing_base + VIDOUT_CON);

	if (ctx->smt311_pipeline_deferred && !ctx->mdnie)
		fimd_smt311_program_rgb_bypass(ctx);

	/* Non-mDNIe pipelines use the direct FIMD bypass path. */
	if (!ctx->mdnie && ctx->sysreg && regmap_update_bits(ctx->sysreg,
				driver_data->lcdblk_offset,
				0x1 << driver_data->lcdblk_bypass_shift,
				0x1 << driver_data->lcdblk_bypass_shift)) {
		DRM_DEV_ERROR(ctx->dev,
			      "Failed to update sysreg for bypass setting.\n");
		return;
	}

	/* TODO: When MIC is enabled for display path, the lcdblk_mic_bypass
	 * bit should be cleared.
	 */
	if (driver_data->has_mic_bypass && ctx->sysreg &&
	    regmap_update_bits(ctx->sysreg,
				driver_data->lcdblk_offset,
				0x1 << driver_data->lcdblk_mic_bypass_shift,
				0x1 << driver_data->lcdblk_mic_bypass_shift)) {
		DRM_DEV_ERROR(ctx->dev,
			      "Failed to update sysreg for bypass mic.\n");
		return;
	}

	/* setup horizontal and vertical display size. */
	val = VIDTCON2_LINEVAL(mode->vdisplay - 1) |
	       VIDTCON2_HOZVAL(mode->hdisplay - 1) |
	       VIDTCON2_LINEVAL_E(mode->vdisplay - 1) |
	       VIDTCON2_HOZVAL_E(mode->hdisplay - 1);
	writel(val, ctx->regs + driver_data->timing_base + VIDTCON2);

	fimd_setup_trigger(ctx);

	ctx->clkdiv = (clkdiv < 0x100) ? clkdiv : 0xff;

	/*
	 * fields of register with prefix '_F' would be updated
	 * at vsync(same as dma start)
	 */
	val = ctx->vidcon0;
	if (ctx->smt311_pipeline_deferred)
		val &= ~(VIDCON0_ENVID | VIDCON0_ENVID_F);
	else
		val |= VIDCON0_ENVID | VIDCON0_ENVID_F;

	/* Match Samsung's FIMD hw_ver 0x70 clock policy on all Exynos4. */
	if (fimd_is_tab3(ctx))
		val &= ~VIDCON0_CLKVALUP;
	if (fimd_is_tab3(ctx))
		val |= VIDCON0_VLCKFREE;

	if (ctx->driver_data->has_clksel)
		val |= VIDCON0_CLKSEL_LCD;

	if (ctx->clkdiv > 1)
		val |= VIDCON0_CLKVAL_F(ctx->clkdiv - 1) | VIDCON0_CLKDIR;

	writel(val, ctx->regs + VIDCON0);
	readl(ctx->regs + VIDCON0);
}

static void fimd_win_set_bldeq(struct fimd_context *ctx, unsigned int win,
			       unsigned int alpha, unsigned int pixel_alpha)
{
	u32 mask = BLENDEQ_A_FUNC_F(0xf) | BLENDEQ_B_FUNC_F(0xf);
	u32 val = 0;

	switch (pixel_alpha) {
	case DRM_MODE_BLEND_PIXEL_NONE:
	case DRM_MODE_BLEND_COVERAGE:
		val |= BLENDEQ_A_FUNC_F(BLENDEQ_ALPHA_A);
		val |= BLENDEQ_B_FUNC_F(BLENDEQ_ONE_MINUS_ALPHA_A);
		break;
	case DRM_MODE_BLEND_PREMULTI:
	default:
		if (alpha != DRM_BLEND_ALPHA_OPAQUE) {
			val |= BLENDEQ_A_FUNC_F(BLENDEQ_ALPHA0);
			val |= BLENDEQ_B_FUNC_F(BLENDEQ_ONE_MINUS_ALPHA_A);
		} else {
			val |= BLENDEQ_A_FUNC_F(BLENDEQ_ONE);
			val |= BLENDEQ_B_FUNC_F(BLENDEQ_ONE_MINUS_ALPHA_A);
		}
		break;
	}
	fimd_set_bits(ctx, BLENDEQx(win), mask, val);
}

static void fimd_win_set_bldmod(struct fimd_context *ctx, unsigned int win,
				unsigned int alpha, unsigned int pixel_alpha)
{
	u32 win_alpha_l = (alpha >> 8) & 0xf;
	u32 win_alpha_h = alpha >> 12;
	u32 val = 0;

	switch (pixel_alpha) {
	case DRM_MODE_BLEND_PIXEL_NONE:
		break;
	case DRM_MODE_BLEND_COVERAGE:
	case DRM_MODE_BLEND_PREMULTI:
	default:
		val |= WINCON1_ALPHA_SEL;
		val |= WINCON1_BLD_PIX;
		val |= WINCON1_ALPHA_MUL;
		break;
	}
	fimd_set_bits(ctx, WINCON(win), WINCONx_BLEND_MODE_MASK, val);

	/* OSD alpha */
	val = VIDISD14C_ALPHA0_R(win_alpha_h) |
		VIDISD14C_ALPHA0_G(win_alpha_h) |
		VIDISD14C_ALPHA0_B(win_alpha_h) |
		VIDISD14C_ALPHA1_R(0x0) |
		VIDISD14C_ALPHA1_G(0x0) |
		VIDISD14C_ALPHA1_B(0x0);
	writel(val, ctx->regs + VIDOSD_C(win));

	val = VIDW_ALPHA_R(win_alpha_l) | VIDW_ALPHA_G(win_alpha_l) |
		VIDW_ALPHA_B(win_alpha_l);
	writel(val, ctx->regs + VIDWnALPHA0(win));

	val = VIDW_ALPHA_R(0x0) | VIDW_ALPHA_G(0x0) |
		VIDW_ALPHA_B(0x0);
	writel(val, ctx->regs + VIDWnALPHA1(win));

	fimd_set_bits(ctx, BLENDCON, BLENDCON_NEW_MASK,
			BLENDCON_NEW_8BIT_ALPHA_VALUE);
}

static void fimd_win_set_pixfmt(struct fimd_context *ctx, unsigned int win,
				struct drm_framebuffer *fb, int width)
{
	struct exynos_drm_plane *plane = &ctx->planes[win];
	struct exynos_drm_plane_state *state =
		to_exynos_plane_state(plane->base.state);
	uint32_t pixel_format = fb->format->format;
	unsigned int alpha = state->base.alpha;
	u32 val = WINCONx_ENWIN;
	unsigned int pixel_alpha;

	if (fb->format->has_alpha)
		pixel_alpha = state->base.pixel_blend_mode;
	else
		pixel_alpha = DRM_MODE_BLEND_PIXEL_NONE;

	/*
	 * Downstream SM-T311 always scans the 32-bit framebuffer as opaque
	 * R8-G8-B8: fb_var_screeninfo.transp.length is zero and FIMD is
	 * programmed with BPPMODE_24BPP_888. Keep the Android buffer memory
	 * layout, but ignore its alpha component in hardware exactly like the
	 * Samsung driver.
	 */
	if (of_machine_is_compatible("samsung,tab3") &&
	    win == ctx->primary_win) {
		u32 requested_format = pixel_format;

		if (pixel_format == DRM_FORMAT_ARGB8888)
			pixel_format = DRM_FORMAT_XRGB8888;
		else if (pixel_format == DRM_FORMAT_ABGR8888)
			pixel_format = DRM_FORMAT_XBGR8888;

		if (pixel_format != requested_format) {
			alpha = DRM_BLEND_ALPHA_OPAQUE;
			pixel_alpha = DRM_MODE_BLEND_PIXEL_NONE;
			dev_info_once(ctx->dev,
				      "SMT311_V134_FIMD_STOCK_OPAQUE_SCANOUT win=%u requested_fourcc=%08x programmed_fourcc=%08x bppmode=24bpp888\n",
				      win, requested_format, pixel_format);
		}
	}

	/*
	 * In case of s3c64xx, window 0 doesn't support alpha channel.
	 * So the request format is ARGB8888 then change it to XRGB8888.
	 */
	if (ctx->driver_data->has_limited_fmt && !win) {
		if (pixel_format == DRM_FORMAT_ARGB8888)
			pixel_format = DRM_FORMAT_XRGB8888;
	}

	switch (pixel_format) {
	case DRM_FORMAT_C8:
		val |= WINCON0_BPPMODE_8BPP_PALETTE;
		val |= WINCONx_BURSTLEN_8WORD;
		val |= WINCONx_BYTSWP;
		break;
	case DRM_FORMAT_XRGB1555:
	case DRM_FORMAT_XBGR1555:
		val |= WINCON0_BPPMODE_16BPP_1555;
		val |= WINCONx_HAWSWP;
		val |= WINCONx_BURSTLEN_16WORD;
		break;
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
		val |= WINCON0_BPPMODE_16BPP_565;
		val |= WINCONx_HAWSWP;
		val |= WINCONx_BURSTLEN_16WORD;
		break;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR8888:
		val |= WINCON0_BPPMODE_24BPP_888;
		val |= WINCONx_WSWP;
		val |= WINCONx_BURSTLEN_16WORD;
		break;
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_ABGR8888:
	default:
		val |= WINCON1_BPPMODE_25BPP_A1888;
		val |= WINCONx_WSWP;
		val |= WINCONx_BURSTLEN_16WORD;
		break;
	}

	switch (pixel_format) {
	case DRM_FORMAT_XBGR1555:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_BGR565:
		writel(WIN_RGB_ORDER_REVERSE, ctx->regs + WIN_RGB_ORDER(win));
		break;
	default:
		writel(WIN_RGB_ORDER_FORWARD, ctx->regs + WIN_RGB_ORDER(win));
		break;
	}

	/*
	 * Setting dma-burst to 16Word causes permanent tearing for very small
	 * buffers, e.g. cursor buffer. Burst Mode switching which based on
	 * plane size is not recommended as plane size varies a lot towards the
	 * end of the screen and rapid movement causes unstable DMA, but it is
	 * still better to change dma-burst than displaying garbage.
	 */

	if (width < MIN_FB_WIDTH_FOR_16WORD_BURST) {
		val &= ~WINCONx_BURSTLEN_MASK;
		val |= WINCONx_BURSTLEN_4WORD;
	}
	fimd_set_bits(ctx, WINCON(win), ~WINCONx_BLEND_MODE_MASK, val);

	/* hardware window 0 doesn't support alpha channel. */
	if (win != 0) {
		fimd_win_set_bldmod(ctx, win, alpha, pixel_alpha);
		fimd_win_set_bldeq(ctx, win, alpha, pixel_alpha);
	}
}

static void fimd_win_set_colkey(struct fimd_context *ctx, unsigned int win)
{
	unsigned int keycon0 = 0, keycon1 = 0;

	keycon0 = ~(WxKEYCON0_KEYBL_EN | WxKEYCON0_KEYEN_F |
			WxKEYCON0_DIRCON) | WxKEYCON0_COMPKEY(0);

	keycon1 = WxKEYCON1_COLVAL(0xffffffff);

	writel(keycon0, ctx->regs + WKEYCON0_BASE(win));
	writel(keycon1, ctx->regs + WKEYCON1_BASE(win));
}

/**
 * fimd_shadow_protect_win() - disable updating values from shadow registers at vsync
 *
 * @ctx: local driver data
 * @win: window to protect registers for
 * @protect: 1 to protect (disable updates)
 */
static void fimd_shadow_protect_win(struct fimd_context *ctx,
				    unsigned int win, bool protect)
{
	u32 reg, bits, val;

	/*
	 * SHADOWCON/PRTCON register is used for enabling timing.
	 *
	 * for example, once only width value of a register is set,
	 * if the dma is started then fimd hardware could malfunction so
	 * with protect window setting, the register fields with prefix '_F'
	 * wouldn't be updated at vsync also but updated once unprotect window
	 * is set.
	 */

	if (ctx->driver_data->has_shadowcon) {
		reg = SHADOWCON;
		bits = SHADOWCON_WINx_PROTECT(win);
	} else {
		reg = PRTCON;
		bits = PRTCON_PROTECT;
	}

	val = readl(ctx->regs + reg);
	if (protect)
		val |= bits;
	else
		val &= ~bits;
	writel(val, ctx->regs + reg);
}

/* Set all shadow window locks in ONE bus transaction.  The old loop
 * unlocked five windows in five separate RMW accesses immediately before
 * the frame's VSYNC latch; if VSYNC fell between writes, it could expose
 * windows updated from different atomic commits for one display frame.
 * Only protect bits are changed; all DMA, blending and clock fields stay.
 * This is a source-level race reduction, NOT a proved physical glitch fix.
 */
static void fimd_shadow_protect_all(struct fimd_context *ctx, bool protect)
{
	u32 reg, bits, val;
	unsigned int i;

	if (ctx->driver_data->has_shadowcon) {
		reg = SHADOWCON;
		bits = 0;
		for (i = 0; i < WINDOWS_NR; i++)
			bits |= SHADOWCON_WINx_PROTECT(i);
	} else {
		reg = PRTCON;
		bits = PRTCON_PROTECT;
	}
	val = readl(ctx->regs + reg);
	val = protect ? val | bits : val & ~bits;
	writel(val, ctx->regs + reg);
}

static void fimd_atomic_begin(struct exynos_drm_crtc *crtc)
{
	struct fimd_context *ctx = crtc->ctx;

	if (ctx->suspended)
		return;

	fimd_shadow_protect_all(ctx, true);
}

static void fimd_atomic_flush(struct exynos_drm_crtc *crtc)
{
	struct fimd_context *ctx = crtc->ctx;

	if (ctx->suspended)
		return;

	fimd_shadow_protect_all(ctx, false);

	exynos_crtc_handle_event(crtc);
}

static void fimd_update_plane(struct exynos_drm_crtc *crtc,
			      struct exynos_drm_plane *plane)
{
	struct exynos_drm_plane_state *state =
				to_exynos_plane_state(plane->base.state);
	struct fimd_context *ctx = crtc->ctx;
	struct drm_framebuffer *fb = state->base.fb;
	dma_addr_t dma_addr;
	unsigned long val, size, offset;
	unsigned int last_x, last_y, buf_offsize, line_size;
	unsigned int win = plane->index;
	unsigned int cpp = fb->format->cpp[0];
	unsigned int pitch = fb->pitches[0];

	if (ctx->suspended)
		return;

	offset = state->src.x * cpp;
	offset += state->src.y * pitch;

	/* buffer start address */
	dma_addr = exynos_drm_fb_dma_addr(fb, 0) + offset;
	val = (unsigned long)dma_addr;
	writel(val, ctx->regs + VIDWx_BUF_START(win, 0));

	/* buffer end address */
	size = pitch * state->crtc.h;
	val = (unsigned long)(dma_addr + size);
	writel(val, ctx->regs + VIDWx_BUF_END(win, 0));

	DRM_DEV_DEBUG_KMS(ctx->dev,
			  "start addr = 0x%lx, end addr = 0x%lx, size = 0x%lx\n",
			  (unsigned long)dma_addr, val, size);
	DRM_DEV_DEBUG_KMS(ctx->dev, "ovl_width = %d, ovl_height = %d\n",
			  state->crtc.w, state->crtc.h);

	/* buffer size */
	buf_offsize = pitch - (state->crtc.w * cpp);
	line_size = state->crtc.w * cpp;
	val = VIDW_BUF_SIZE_OFFSET(buf_offsize) |
		VIDW_BUF_SIZE_PAGEWIDTH(line_size) |
		VIDW_BUF_SIZE_OFFSET_E(buf_offsize) |
		VIDW_BUF_SIZE_PAGEWIDTH_E(line_size);
	writel(val, ctx->regs + VIDWx_BUF_SIZE(win, 0));

	/* OSD position */
	val = VIDOSDxA_TOPLEFT_X(state->crtc.x) |
		VIDOSDxA_TOPLEFT_Y(state->crtc.y) |
		VIDOSDxA_TOPLEFT_X_E(state->crtc.x) |
		VIDOSDxA_TOPLEFT_Y_E(state->crtc.y);
	writel(val, ctx->regs + VIDOSD_A(win));

	last_x = state->crtc.x + state->crtc.w;
	if (last_x)
		last_x--;
	last_y = state->crtc.y + state->crtc.h;
	if (last_y)
		last_y--;

	val = VIDOSDxB_BOTRIGHT_X(last_x) | VIDOSDxB_BOTRIGHT_Y(last_y) |
		VIDOSDxB_BOTRIGHT_X_E(last_x) | VIDOSDxB_BOTRIGHT_Y_E(last_y);

	writel(val, ctx->regs + VIDOSD_B(win));

	DRM_DEV_DEBUG_KMS(ctx->dev,
			  "osd pos: tx = %d, ty = %d, bx = %d, by = %d\n",
			  state->crtc.x, state->crtc.y, last_x, last_y);

	/* OSD size */
	if (win != 3 && win != 4) {
		u32 offset = VIDOSD_D(win);
		if (win == 0)
			offset = VIDOSD_C(win);
		val = state->crtc.w * state->crtc.h;
		writel(val, ctx->regs + offset);

		DRM_DEV_DEBUG_KMS(ctx->dev, "osd size = 0x%x\n",
				  (unsigned int)val);
	}

	fimd_win_set_pixfmt(ctx, win, fb, state->src.w);

	/* hardware window 0 doesn't support color key. */
	if (win != 0)
		fimd_win_set_colkey(ctx, win);

	fimd_enable_video_output(ctx, win, true);

	if (ctx->driver_data->has_shadowcon)
		fimd_enable_shadow_channel_path(ctx, win, true);

	if (ctx->i80_if)
		atomic_set(&ctx->win_updated, 1);
}

static void fimd_disable_plane(struct exynos_drm_crtc *crtc,
			       struct exynos_drm_plane *plane)
{
	struct fimd_context *ctx = crtc->ctx;
	unsigned int win = plane->index;

	if (ctx->suspended)
		return;

	fimd_enable_video_output(ctx, win, false);

	if (ctx->driver_data->has_shadowcon)
		fimd_enable_shadow_channel_path(ctx, win, false);
}

/* Covers the Wi-Fi (T310), 3G (T311) and LTE (T315) variants. */
static bool fimd_is_tab3(struct fimd_context *ctx)
{
	return ctx->driver_data == &exynos4_fimd_driver_data &&
		of_machine_is_compatible("samsung,tab3");
}

static bool fimd_is_tab3_dsi_pipeline(struct fimd_context *ctx)
{
	return !ctx->i80_if && fimd_is_tab3(ctx);
}

static void fimd_preload_planes_before_dsim(struct exynos_drm_crtc *crtc)
{
	struct fimd_context *ctx = crtc->ctx;
	int i;

	for (i = 0; i < WINDOWS_NR; i++)
		fimd_shadow_protect_win(ctx, i, true);
	for (i = 0; i < WINDOWS_NR; i++) {
		struct drm_plane_state *state = ctx->planes[i].base.state;

		if (state && state->crtc == &crtc->base && state->fb)
			fimd_update_plane(crtc, &ctx->planes[i]);
		else
			fimd_disable_plane(crtc, &ctx->planes[i]);
	}
	for (i = 0; i < WINDOWS_NR; i++)
		fimd_shadow_protect_win(ctx, i, false);
	if (ctx->driver_data->has_shadowcon)
		readl(ctx->regs + SHADOWCON);
	else
		readl(ctx->regs + PRTCON);
}

static void fimd_smt311_set_active_planes(struct exynos_drm_crtc *crtc,
					  bool enable)
{
	struct fimd_context *ctx = crtc->ctx;
	unsigned int win;

	for (win = 0; win < WINDOWS_NR; win++) {
		struct drm_plane_state *state = ctx->planes[win].base.state;

		if (!state || state->crtc != &crtc->base || !state->fb)
			continue;

		/* Samsung s3cfb_window_on/off() register order. */
		if (enable) {
			if (ctx->driver_data->has_shadowcon)
				fimd_enable_shadow_channel_path(ctx, win, true);
			fimd_enable_video_output(ctx, win, true);
		} else {
			fimd_enable_video_output(ctx, win, false);
			if (ctx->driver_data->has_shadowcon)
				fimd_enable_shadow_channel_path(ctx, win, false);
		}
	}

	if (ctx->driver_data->has_shadowcon)
		readl(ctx->regs + SHADOWCON);
	else
		readl(ctx->regs + PRTCON);
}

static void fimd_smt311_dump_state(struct fimd_context *ctx,
				   const char *marker)
{
	void __iomem *timing_base = ctx->regs + ctx->driver_data->timing_base;
	struct drm_display_mode *m = &ctx->crtc->base.state->adjusted_mode;
	unsigned int primary_win = ctx->primary_win;
	struct drm_plane_state *primary = ctx->planes[primary_win].base.state;
	struct drm_framebuffer *fb = primary ? primary->fb : NULL;
	u32 fourcc = fb ? fb->format->format : 0;
	u32 vidosd_c = primary_win < 3 ?
		readl(ctx->regs + VIDOSD_C(primary_win)) : 0;
	u32 lcdblk = 0;
	int lcdblk_read = ctx->sysreg ?
		regmap_read(ctx->sysreg, ctx->driver_data->lcdblk_offset,
			    &lcdblk) : -ENODEV;
	unsigned long lcd_clk = clk_get_rate(ctx->lcd_clk);
	unsigned long effective_pclk = ctx->clkdiv ? lcd_clk / ctx->clkdiv : 0;

	DRM_DEV_INFO(ctx->dev,
		     "%s seq=%u primary_win=%u iommu_supported=%d mode_pclk_khz=%d effective_pclk_hz=%lu hfp=%d hbp=%d hsa=%d vfp=%d vbp=%d vsa=%d lcd_clk_hz=%lu bus_clk_hz=%lu clkdiv=%u fourcc=%08x fb_width=%u fb_height=%u fb_pitch0=%u fb_offset0=%u vidcon0=%08x vidcon1=%08x vidtcon0=%08x vidtcon1=%08x vidtcon2=%08x wincon_primary=%08x rgb_order_primary=%08x shadowcon=%08x buf_start_primary=%08x buf_end_primary=%08x buf_size_primary=%08x vidosd_a_primary=%08x vidosd_b_primary=%08x vidosd_c_primary=%08x vidintcon0=%08x lcdblk_read=%d lcdblk=%08x lcdblk_vt_shift=%u lcdblk_bypass_shift=%u\n",
		     marker, ctx->smt311_pipeline_seq, primary_win,
		     is_drm_iommu_supported(ctx->drm_dev), m->clock,
		     effective_pclk, m->hsync_start - m->hdisplay,
		     m->htotal - m->hsync_end,
		     m->hsync_end - m->hsync_start,
		     m->vsync_start - m->vdisplay,
		     m->vtotal - m->vsync_end,
		     m->vsync_end - m->vsync_start, lcd_clk,
		     clk_get_rate(ctx->bus_clk), ctx->clkdiv, fourcc,
		     fb ? fb->width : 0, fb ? fb->height : 0,
		     fb ? fb->pitches[0] : 0, fb ? fb->offsets[0] : 0,
		     readl(ctx->regs + VIDCON0), readl(timing_base + VIDCON1),
		     readl(timing_base + VIDTCON0), readl(timing_base + VIDTCON1),
		     readl(timing_base + VIDTCON2), readl(ctx->regs + WINCON(primary_win)),
		     readl(ctx->regs + WIN_RGB_ORDER(primary_win)),
		     readl(ctx->regs + SHADOWCON),
		     readl(ctx->regs + VIDWx_BUF_START(primary_win, 0)),
		     readl(ctx->regs + VIDWx_BUF_END(primary_win, 0)),
		     readl(ctx->regs + VIDWx_BUF_SIZE(primary_win, 0)),
		     readl(ctx->regs + VIDOSD_A(primary_win)),
		     readl(ctx->regs + VIDOSD_B(primary_win)), vidosd_c,
		     readl(ctx->regs + VIDINTCON0),
		     lcdblk_read, lcdblk, ctx->driver_data->lcdblk_vt_shift,
		     ctx->driver_data->lcdblk_bypass_shift);

	DRM_DEV_INFO(ctx->dev,
		     "%s SMT311_V132_FIMD_WINDOWS vidcon2=%08x dualrgb=%08x "
		     "wincon=[%08x,%08x,%08x,%08x,%08x] "
		     "winmap=[%08x,%08x,%08x,%08x,%08x] "
		     "buf0=%08x/%08x/%08x buf3=%08x/%08x/%08x\n",
		     marker, readl(ctx->regs + VIDCON2),
		     readl(ctx->regs + DUALRGB),
		     readl(ctx->regs + WINCON(0)),
		     readl(ctx->regs + WINCON(1)),
		     readl(ctx->regs + WINCON(2)),
		     readl(ctx->regs + WINCON(3)),
		     readl(ctx->regs + WINCON(4)),
		     readl(ctx->regs + WINxMAP(0)),
		     readl(ctx->regs + WINxMAP(1)),
		     readl(ctx->regs + WINxMAP(2)),
		     readl(ctx->regs + WINxMAP(3)),
		     readl(ctx->regs + WINxMAP(4)),
		     readl(ctx->regs + VIDWx_BUF_START(0, 0)),
		     readl(ctx->regs + VIDWx_BUF_END(0, 0)),
		     readl(ctx->regs + VIDWx_BUF_SIZE(0, 0)),
		     readl(ctx->regs + VIDWx_BUF_START(3, 0)),
		     readl(ctx->regs + VIDWx_BUF_END(3, 0)),
		     readl(ctx->regs + VIDWx_BUF_SIZE(3, 0)));
}

static int fimd_smt311_verify_prestart(struct fimd_context *ctx)
{
	void __iomem *timing_base = ctx->regs + ctx->driver_data->timing_base;
	unsigned int primary_win = ctx->primary_win;
	struct drm_plane_state *primary = ctx->planes[primary_win].base.state;
	u32 vidcon0 = readl(ctx->regs + VIDCON0);
	u32 vidcon1 = readl(timing_base + VIDCON1);
	u32 vidcon2 = readl(ctx->regs + VIDCON2);
	u32 wincon_primary = readl(ctx->regs + WINCON(primary_win));
	u32 rgb_mask = VIDCON2_EN601 | VIDCON2_WB |
		VIDCON2_TVFMTSEL_SW | VIDCON2_TVFMTSEL1_MASK |
		VIDCON2_ORGYCbCr | VIDCON2_YUVORDCrCb;
	unsigned int win;

	if (is_drm_iommu_supported(ctx->drm_dev)) {
		DRM_DEV_ERROR(ctx->dev, "SMT311_V127_FIMD_PRESTART_FAIL iommu_still_enabled\n");
		return -EINVAL;
	}
	if (primary_win != 2) {
		DRM_DEV_ERROR(ctx->dev,
			      "TAB3_V136_FIMD_PRESTART_FAIL primary_win=%u expected=2\n",
			      primary_win);
		return -EINVAL;
	}
	if (!primary || primary->crtc != &ctx->crtc->base || !primary->fb) {
		DRM_DEV_ERROR(ctx->dev, "SMT311_V127_FIMD_PRESTART_FAIL primary_not_ready\n");
		return -EINVAL;
	}
	if (vidcon2 & rgb_mask) {
		DRM_DEV_ERROR(ctx->dev,
			      "SMT311_V132_FIMD_PRESTART_FAIL vidcon2=%08x mask=%08x\n",
			      vidcon2, rgb_mask);
		return -EINVAL;
	}
	if (!ctx->mdnie &&
	    readl(ctx->regs + DUALRGB) != DUALRGB_BYPASS_SINGLE) {
		DRM_DEV_ERROR(ctx->dev,
			      "TAB3_FIMD_PRESTART_FAIL bypass_dualrgb=%08x\n",
			      readl(ctx->regs + DUALRGB));
		return -EINVAL;
	}
	for (win = 0; win < WINDOWS_NR; win++) {
		struct drm_plane_state *state = ctx->planes[win].base.state;
		u32 wincon = readl(ctx->regs + WINCON(win));
		u32 winmap = readl(ctx->regs + WINxMAP(win));
		bool active = state && state->crtc == &ctx->crtc->base &&
			state->fb;

		if (!active && (wincon & WINCONx_ENWIN)) {
			DRM_DEV_ERROR(ctx->dev,
				      "SMT311_V132_FIMD_PRESTART_FAIL stale_win=%u wincon=%08x\n",
				      win, wincon);
			return -EINVAL;
		}
		if (winmap) {
			DRM_DEV_ERROR(ctx->dev,
				      "SMT311_V132_FIMD_PRESTART_FAIL winmap%u=%08x\n",
				      win, winmap);
			return -EINVAL;
		}
	}
	if (vidcon0 & (VIDCON0_ENVID | VIDCON0_ENVID_F)) {
		DRM_DEV_ERROR(ctx->dev, "SMT311_V127_FIMD_PRESTART_FAIL envid_already_high=%08x\n", vidcon0);
		return -EINVAL;
	}
	if ((vidcon0 & VIDCON0_CLKVALUP) ||
	    !(vidcon0 & VIDCON0_VLCKFREE)) {
		DRM_DEV_ERROR(ctx->dev,
			      "EXYNOS4_V136_FIMD_PRESTART_FAIL stock_clock_policy=%08x\n",
			      vidcon0);
		return -EINVAL;
	}
	DRM_DEV_INFO(ctx->dev,
		     "EXYNOS4_V136_FIMD_STOCK_CLOCK_POLICY vidcon0=%08x\n",
		     vidcon0);
	if (vidcon1 & VIDCON1_VCLK_MASK) {
		DRM_DEV_ERROR(ctx->dev,
			      "TAB3_V143_FIMD_PRESTART_FAIL nonstock_vclk=%08x\n",
			      vidcon1);
		return -EINVAL;
	}
	DRM_DEV_INFO(ctx->dev,
		     "TAB3_V143_FIMD_STOCK_VCLK vidcon1=%08x inv_vclk=%u\n",
		     vidcon1, !!(vidcon1 & VIDCON1_INV_VCLK));
	if ((wincon_primary & WINCONx_ENWIN) ||
	    (wincon_primary & WINCONx_BURSTLEN_MASK) !=
	    WINCONx_BURSTLEN_16WORD ||
	    (ctx->driver_data->has_shadowcon &&
	     (readl(ctx->regs + SHADOWCON) &
	      SHADOWCON_CHx_ENABLE(primary_win)))) {
		DRM_DEV_ERROR(ctx->dev,
			      "TAB3_V141_FIMD_PRESTART_FAIL primary_win=%u wincon=%08x shadowcon=%08x\n",
			      primary_win, wincon_primary,
			      readl(ctx->regs + SHADOWCON));
		return -EINVAL;
	}
	DRM_DEV_INFO(ctx->dev,
		     "TAB3_V141_FIMD_PRESTART_STOCK_WINDOW_OFF primary_win=%u vidcon0=%08x vidcon1=%08x vidcon2=%08x dualrgb=%08x wincon0=%08x wincon2=%08x shadowcon=%08x iommu=0 burst=16\n",
		     primary_win, vidcon0, vidcon1, vidcon2,
		     readl(ctx->regs + DUALRGB), readl(ctx->regs + WINCON(0)),
		     readl(ctx->regs + WINCON(2)),
		     readl(ctx->regs + SHADOWCON));
	return 0;
}

static int fimd_smt311_pipeline_enable(struct exynos_drm_crtc *crtc)
{
	struct fimd_context *ctx = crtc ? crtc->ctx : NULL;
	unsigned int primary_win;
	u32 val;
	int ret;

	if (!ctx || !ctx->smt311_pipeline_deferred)
		return 0;
	if (ctx->suspended) {
		DRM_DEV_ERROR(ctx->dev, "SMT311_V127_FIMD_START_REJECTED_SUSPENDED\n");
		return -EHOSTDOWN;
	}
	if (ctx->smt311_pipeline_started)
		return 0;

	ret = fimd_smt311_verify_prestart(ctx);
	if (ret)
		return ret;

	/* Samsung: route + mDNIe geometry + IELCD, with FIMD window off. */
	if (ctx->mdnie) {
		unsigned long pixel_rate = clk_get_rate(ctx->lcd_clk);

		ret = exynos4_mdnie_prepare(ctx->mdnie, ctx->regs, pixel_rate);
		if (ret) {
			DRM_DEV_ERROR(ctx->dev,
				      "TAB3_V141_MDNIE_PREPARE_FAIL ret=%d\n", ret);
			return ret;
		}

		writel(DUALRGB_MDNIE, ctx->regs + DUALRGB);
		readl(ctx->regs + DUALRGB);

		ret = exynos4_mdnie_enable(ctx->mdnie);
		if (ret) {
			writel(DUALRGB_BYPASS_SINGLE, ctx->regs + DUALRGB);
			exynos4_mdnie_unprepare(ctx->mdnie);
			return ret;
		}
	}

	/* Samsung s3cfb_display_on(): global FIMD before enabling DMA window. */
	val = readl(ctx->regs + VIDCON0);
	val |= VIDCON0_ENVID | VIDCON0_ENVID_F;
	writel(val, ctx->regs + VIDCON0);
	val = readl(ctx->regs + VIDCON0);
	if ((val & (VIDCON0_ENVID | VIDCON0_ENVID_F)) !=
	    (VIDCON0_ENVID | VIDCON0_ENVID_F)) {
		DRM_DEV_ERROR(ctx->dev,
			      "TAB3_FIMD_START_READBACK_FAIL vidcon0=%08x\n",
			      val);
		if (ctx->mdnie) {
			exynos4_mdnie_disable(ctx->mdnie);
			writel(DUALRGB_BYPASS_SINGLE, ctx->regs + DUALRGB);
			exynos4_mdnie_unprepare(ctx->mdnie);
		}
		return -EIO;
	}

	/* Samsung s3cfb_window_on(): SHADOWCON first, ENWIN second. */
	fimd_smt311_set_active_planes(crtc, true);
	primary_win = ctx->primary_win;
	if (!(readl(ctx->regs + WINCON(primary_win)) & WINCONx_ENWIN) ||
	    (ctx->driver_data->has_shadowcon &&
	     !(readl(ctx->regs + SHADOWCON) &
	       SHADOWCON_CHx_ENABLE(primary_win)))) {
		DRM_DEV_ERROR(ctx->dev,
			      "TAB3_V144_FIMD_WINDOW_ENABLE_FAIL primary_win=%u wincon=%08x shadowcon=%08x\n",
			      primary_win,
			      readl(ctx->regs + WINCON(primary_win)),
			      readl(ctx->regs + SHADOWCON));
		val = readl(ctx->regs + VIDCON0);
		val |= VIDCON0_ENVID;
		val &= ~VIDCON0_ENVID_F;
		writel(val, ctx->regs + VIDCON0);
		if (ctx->mdnie) {
			exynos4_mdnie_disable(ctx->mdnie);
			writel(DUALRGB_BYPASS_SINGLE, ctx->regs + DUALRGB);
			exynos4_mdnie_unprepare(ctx->mdnie);
		}
		return -EIO;
	}

	ctx->smt311_pipeline_started = true;
	ctx->smt311_pipeline_seq++;

	/* Stock mdnie_probe() applies LT01 after FIMD and its window are active. */
	if (ctx->mdnie) {
		ret = exynos4_mdnie_apply_tuning(ctx->mdnie);
		if (ret)
			DRM_DEV_ERROR(ctx->dev,
				      "TAB3_V141_MDNIE_LATE_TUNING_FAIL ret=%d\n",
				      ret);
	}

	DRM_DEV_INFO(ctx->dev,
		     "TAB3_V141_STOCK_START_ORDER_READY TAB3_V144_EXACT_STOCK_START_ORDER dualrgb=%08x primary_win=%u wincon=%08x shadowcon=%08x vidcon0=%08x\n",
		     readl(ctx->regs + DUALRGB), primary_win,
		     readl(ctx->regs + WINCON(primary_win)),
		     readl(ctx->regs + SHADOWCON),
		     readl(ctx->regs + VIDCON0));
	fimd_smt311_dump_state(ctx, "SMT311_V144_FIMD_EXACT_STOCK_ORDER");
	return 0;
}

static void fimd_smt311_pipeline_disable(struct exynos_drm_crtc *crtc)
{
	struct fimd_context *ctx = crtc ? crtc->ctx : NULL;
	u32 val;
	unsigned int timeout;

	if (!ctx || !ctx->smt311_pipeline_deferred ||
	    !ctx->smt311_pipeline_started || ctx->suspended)
		return;

	/* Samsung s3cfb_display_off(): ENVID stays set, clear ENVID_F. */
	val = readl(ctx->regs + VIDCON0);
	val |= VIDCON0_ENVID;
	val &= ~VIDCON0_ENVID_F;
	writel(val, ctx->regs + VIDCON0);
	for (timeout = 0; timeout < 2000000; timeout++) {
		if (!(readl(ctx->regs + VIDCON0) & VIDCON0_ENVID_F))
			break;
		cpu_relax();
	}
	if (timeout == 2000000)
		DRM_DEV_ERROR(ctx->dev,
			      "TAB3_V144_FIMD_FRAME_STOP_TIMEOUT vidcon0=%08x\n",
			      readl(ctx->regs + VIDCON0));

	/* Samsung turns IELCD off only after FIMD frame output has stopped. */
	if (ctx->mdnie)
		exynos4_mdnie_disable(ctx->mdnie);

	fimd_smt311_set_active_planes(crtc, false);

	if (ctx->mdnie) {
		writel(DUALRGB_BYPASS_SINGLE, ctx->regs + DUALRGB);
		readl(ctx->regs + DUALRGB);
		exynos4_mdnie_unprepare(ctx->mdnie);
	}

	ctx->smt311_pipeline_started = false;
	fimd_smt311_dump_state(ctx, "SMT311_V144_FIMD_STOP_EXACT_STOCK_ORDER");
}

static void fimd_atomic_enable(struct exynos_drm_crtc *crtc)
{
	struct fimd_context *ctx = crtc->ctx;

	if (!ctx->suspended)
		return;
	ctx->suspended = false;
	ctx->smt311_pipeline_deferred = fimd_is_tab3_dsi_pipeline(ctx);
	ctx->smt311_pipeline_started = false;
	if (pm_runtime_resume_and_get(ctx->dev) < 0) {
		dev_warn(ctx->dev, "failed to enable FIMD device.\n");
		ctx->suspended = true;
		ctx->smt311_pipeline_deferred = false;
		return;
	}
	if (test_and_clear_bit(0, &ctx->irq_flags))
		fimd_enable_vblank(ctx->crtc);
	fimd_preload_planes_before_dsim(ctx->crtc);
	if (ctx->smt311_pipeline_deferred)
		fimd_smt311_set_active_planes(ctx->crtc, false);
	fimd_commit(ctx->crtc);
	if (ctx->smt311_pipeline_deferred)
		fimd_smt311_dump_state(ctx, "SMT311_V141_FIMD_ARMED_WINDOW_OFF_NO_ENVID");
}

static void fimd_atomic_disable(struct exynos_drm_crtc *crtc)
{
	struct fimd_context *ctx = crtc->ctx;
	int i;

	if (ctx->suspended)
		return;
	if (unlikely(ctx->fifo_diag_registered)) {
		unsigned long flags;
		u32 val;

		/* Mask the interrupt before FIMD clocks are disabled. */
		spin_lock_irqsave(&ctx->fifo_irq_lock, flags);
		val = readl(ctx->regs + VIDINTCON0);
		val &= ~FIMD_FIFO_IRQ_ENABLE;
		writel(val, ctx->regs + VIDINTCON0);
		spin_unlock_irqrestore(&ctx->fifo_irq_lock, flags);
		atomic_set(&ctx->fifo_diag_active_vblanks, 0);
	}
	if (ctx->smt311_pipeline_deferred) {
		fimd_smt311_pipeline_disable(crtc);
	} else {
		for (i = 0; i < WINDOWS_NR; i++)
			fimd_disable_plane(crtc, &ctx->planes[i]);
		fimd_enable_vblank(crtc);
		fimd_wait_for_vblank(crtc);
		fimd_disable_vblank(crtc);
		writel(0, ctx->regs + VIDCON0);
	}
	pm_runtime_put_sync(ctx->dev);
	ctx->suspended = true;
	ctx->smt311_pipeline_deferred = false;
	ctx->smt311_pipeline_started = false;
}

static void fimd_trigger(struct device *dev)
{
	struct fimd_context *ctx = dev_get_drvdata(dev);
	const struct fimd_driver_data *driver_data = ctx->driver_data;
	void *timing_base = ctx->regs + driver_data->timing_base;
	u32 reg;

	 /*
	  * Skips triggering if in triggering state, because multiple triggering
	  * requests can cause panel reset.
	  */
	if (atomic_read(&ctx->triggering))
		return;

	/* Enters triggering mode */
	atomic_set(&ctx->triggering, 1);

	reg = readl(timing_base + TRIGCON);
	reg |= (TRGMODE_ENABLE | SWTRGCMD_ENABLE);
	writel(reg, timing_base + TRIGCON);

	/*
	 * Exits triggering mode if vblank is not enabled yet, because when the
	 * VIDINTCON0 register is not set, it can not exit from triggering mode.
	 */
	if (!test_bit(0, &ctx->irq_flags))
		atomic_set(&ctx->triggering, 0);
}

static void fimd_te_handler(struct exynos_drm_crtc *crtc)
{
	struct fimd_context *ctx = crtc->ctx;
	u32 trg_type = ctx->driver_data->trg_type;

	/* Checks the crtc is detached already from encoder */
	if (!ctx->drm_dev)
		return;

	if (trg_type == I80_HW_TRG)
		goto out;

	/*
	 * If there is a page flip request, triggers and handles the page flip
	 * event so that current fb can be updated into panel GRAM.
	 */
	if (atomic_add_unless(&ctx->win_updated, -1, 0))
		fimd_trigger(ctx->dev);

out:
	/* Wakes up vsync event queue */
	if (atomic_read(&ctx->wait_vsync_event)) {
		atomic_set(&ctx->wait_vsync_event, 0);
		wake_up(&ctx->wait_vsync_queue);
	}

	if (test_bit(0, &ctx->irq_flags))
		drm_crtc_handle_vblank(&ctx->crtc->base);
}

static void fimd_dp_clock_enable(struct exynos_drm_clk *clk, bool enable)
{
	struct fimd_context *ctx = container_of(clk, struct fimd_context,
						dp_clk);
	u32 val = enable ? DP_MIE_CLK_DP_ENABLE : DP_MIE_CLK_DISABLE;

	if (enable == ctx->dp_clk_enabled)
		return;

	if (enable)
		pm_runtime_resume_and_get(ctx->dev);

	ctx->dp_clk_enabled = enable;
	writel(val, ctx->regs + DP_MIE_CLKCON);

	if (!enable)
		pm_runtime_put(ctx->dev);
}

static const struct exynos_drm_crtc_ops fimd_crtc_ops = {
	.atomic_enable = fimd_atomic_enable,
	.atomic_disable = fimd_atomic_disable,
	.enable_vblank = fimd_enable_vblank,
	.disable_vblank = fimd_disable_vblank,
	.atomic_begin = fimd_atomic_begin,
	.update_plane = fimd_update_plane,
	.disable_plane = fimd_disable_plane,
	.atomic_flush = fimd_atomic_flush,
	.atomic_check = fimd_atomic_check,
	.smt311_pipeline_enable = fimd_smt311_pipeline_enable,
	.smt311_pipeline_disable = fimd_smt311_pipeline_disable,
	.te_handler = fimd_te_handler,
};

static irqreturn_t fimd_irq_handler(int irq, void *dev_id)
{
	struct fimd_context *ctx = (struct fimd_context *)dev_id;
	u32 val, clear_bit;

	val = readl(ctx->regs + VIDINTCON1);

	clear_bit = ctx->i80_if ? VIDINTCON1_INT_I80 : VIDINTCON1_INT_FRAME;
	/*
	 * Only a genuine frame-complete (or i80 interface-done) interrupt
	 * advances DRM's vblank timeline or wakes a VSYNC waiter. Other FIMD
	 * interrupt sources share this status register; previously a FIFO
	 * underrun or a stale/empty status could be treated as a new frame.
	 *
	 * Keep the existing IRQ return value for this non-shared line. The
	 * owner of any separately enabled FIFO source must clear its W1C
	 * status; never misreport its interrupt as a completed DRM frame.
	 */
	if (!(val & clear_bit))
		goto out;
	writel(clear_bit, ctx->regs + VIDINTCON1);

	/* check the crtc is detached already from encoder */
	if (!ctx->drm_dev)
		goto out;

	if (!ctx->i80_if) {
		drm_crtc_handle_vblank(&ctx->crtc->base);
		if (unlikely(ctx->fifo_diag_registered &&
			     ctx->smt311_pipeline_started &&
			     !ctx->fifo_diag_latched &&
			     atomic_inc_return(&ctx->fifo_diag_active_vblanks) == 3))
			fimd_fifo_arm_after_vblank(ctx);
	}

	if (ctx->i80_if) {
		/* Exits triggering mode */
		atomic_set(&ctx->triggering, 0);
	} else {
		/* set wait vsync event to zero and wake up queue. */
		if (atomic_read(&ctx->wait_vsync_event)) {
			atomic_set(&ctx->wait_vsync_event, 0);
			wake_up(&ctx->wait_vsync_queue);
		}
	}

out:
	return IRQ_HANDLED;
}

static irqreturn_t fimd_fifo_one_shot_irq(int irq, void *dev_id)
{
	struct fimd_context *ctx = dev_id;
	unsigned long flags;
	u32 status, val;
	bool first = false;

	spin_lock_irqsave(&ctx->fifo_irq_lock, flags);
	status = readl(ctx->regs + VIDINTCON1);
	if (status & VIDINTCON1_INT_FIFO) {
		/* W1C status, then MASK the producer: no interrupt storm. */
		writel(VIDINTCON1_INT_FIFO, ctx->regs + VIDINTCON1);
		val = readl(ctx->regs + VIDINTCON0);
		val &= ~FIMD_FIFO_IRQ_ENABLE;
		writel(val, ctx->regs + VIDINTCON0);
		if (!ctx->fifo_diag_latched) {
			ctx->fifo_diag_latched = true;
			first = true;
		}
	}
	spin_unlock_irqrestore(&ctx->fifo_irq_lock, flags);
	if (first)
		dev_warn(ctx->dev,
			 "SMT311_FIMD_FIFO_EMPTY_ONCE selected_sources=0x%x (IRQ masked)\n",
			 ctx->fifo_diag_window_mask);
	return (status & VIDINTCON1_INT_FIFO) ? IRQ_HANDLED : IRQ_NONE;
}

static int fimd_bind(struct device *dev, struct device *master, void *data)
{
	struct fimd_context *ctx = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct exynos_drm_plane *exynos_plane;
	bool tab3 = fimd_is_tab3(ctx);
	unsigned int i;
	int ret;

	ctx->drm_dev = drm_dev;
	ctx->primary_win = tab3 ? 2 : DEFAULT_WIN;

	for (i = 0; i < WINDOWS_NR; i++) {
		if (tab3 && i == 0) {
			ctx->configs[i].pixel_formats = fimd_exynos4_win0_formats;
			ctx->configs[i].num_pixel_formats =
				ARRAY_SIZE(fimd_exynos4_win0_formats);
		} else if (ctx->driver_data->has_bgr_support) {
			ctx->configs[i].pixel_formats = fimd_extended_formats;
			ctx->configs[i].num_pixel_formats = ARRAY_SIZE(fimd_extended_formats);
		} else {
			ctx->configs[i].pixel_formats = fimd_formats;
			ctx->configs[i].num_pixel_formats = ARRAY_SIZE(fimd_formats);
		}

		ctx->configs[i].zpos = i;
		ctx->configs[i].type = fimd_win_types[i];
		if (tab3) {
			if (i == DEFAULT_WIN)
				ctx->configs[i].type = DRM_PLANE_TYPE_OVERLAY;
			else if (i == ctx->primary_win)
				ctx->configs[i].type = DRM_PLANE_TYPE_PRIMARY;
		}
		ctx->configs[i].capabilities = capabilities[i];
		ret = exynos_plane_init(drm_dev, &ctx->planes[i], i,
					&ctx->configs[i]);
		if (ret)
			return ret;
	}

	exynos_plane = &ctx->planes[ctx->primary_win];
	ctx->crtc = exynos_drm_crtc_create(drm_dev, &exynos_plane->base,
			EXYNOS_DISPLAY_TYPE_LCD, &fimd_crtc_ops, ctx);
	if (IS_ERR(ctx->crtc))
		return PTR_ERR(ctx->crtc);

	if (tab3)
		DRM_DEV_INFO(ctx->dev,
			     "TAB3_V136_FIMD_STOCK_PRIMARY_WINDOW primary_win=%u win0_type=%u win2_type=%u\n",
			     ctx->primary_win, ctx->configs[0].type,
			     ctx->configs[2].type);

	if (ctx->driver_data->has_dp_clk) {
		ctx->dp_clk.enable = fimd_dp_clock_enable;
		ctx->crtc->pipe_clk = &ctx->dp_clk;
	}

	if (ctx->encoder)
		exynos_dpi_bind(drm_dev, ctx->encoder);

	/*
	 * The generic code used to clear inherited channels only when an IOMMU
	 * was active.  SM-T311 deliberately runs FIMD without an IOMMU, so
	 * clear any bootloader scanout before programming the DRM state.
	 */
	if (is_drm_iommu_supported(drm_dev) || fimd_is_tab3(ctx)) {
		ret = fimd_clear_channels(ctx->crtc, fimd_is_tab3(ctx));
		if (ret < 0)
			return ret;
	}

	return exynos_drm_register_dma(drm_dev, dev, &ctx->dma_priv);
}

static void fimd_unbind(struct device *dev, struct device *master,
			void *data)
{
	struct fimd_context *ctx = dev_get_drvdata(dev);

	fimd_atomic_disable(ctx->crtc);

	exynos_drm_unregister_dma(ctx->drm_dev, ctx->dev, &ctx->dma_priv);

	if (ctx->encoder)
		exynos_dpi_remove(ctx->encoder);
}

static const struct component_ops fimd_component_ops = {
	.bind	= fimd_bind,
	.unbind = fimd_unbind,
};

static void fimd_mdnie_put_action(void *data)
{
	exynos4_mdnie_put(data);
}

static int fimd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fimd_context *ctx;
	struct device_node *i80_if_timings;
	int ret;

	if (!dev->of_node)
		return -ENODEV;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dev = dev;
	ctx->suspended = true;
	ctx->driver_data = of_device_get_match_data(dev);

	if (of_property_read_bool(dev->of_node, "samsung,invert-vden"))
		ctx->vidcon1 |= VIDCON1_INV_VDEN;
	if (of_property_read_bool(dev->of_node, "samsung,invert-vclk"))
		ctx->vidcon1 |= VIDCON1_INV_VCLK;

	i80_if_timings = of_get_child_by_name(dev->of_node, "i80-if-timings");
	if (i80_if_timings) {
		u32 val;

		ctx->i80_if = true;

		if (ctx->driver_data->has_vidoutcon)
			ctx->vidout_con |= VIDOUT_CON_F_I80_LDI0;
		else
			ctx->vidcon0 |= VIDCON0_VIDOUT_I80_LDI0;
		/*
		 * The user manual describes that this "DSI_EN" bit is required
		 * to enable I80 24-bit data interface.
		 */
		ctx->vidcon0 |= VIDCON0_DSI_EN;

		if (of_property_read_u32(i80_if_timings, "cs-setup", &val))
			val = 0;
		ctx->i80ifcon = LCD_CS_SETUP(val);
		if (of_property_read_u32(i80_if_timings, "wr-setup", &val))
			val = 0;
		ctx->i80ifcon |= LCD_WR_SETUP(val);
		if (of_property_read_u32(i80_if_timings, "wr-active", &val))
			val = 1;
		ctx->i80ifcon |= LCD_WR_ACTIVE(val);
		if (of_property_read_u32(i80_if_timings, "wr-hold", &val))
			val = 0;
		ctx->i80ifcon |= LCD_WR_HOLD(val);
	}
	of_node_put(i80_if_timings);

	ctx->sysreg = syscon_regmap_lookup_by_phandle(dev->of_node,
							"samsung,sysreg");
	if (IS_ERR(ctx->sysreg)) {
		dev_warn(dev, "failed to get system register.\n");
		ctx->sysreg = NULL;
	}

	if (of_find_property(dev->of_node, "samsung,mdnie", NULL)) {
		ctx->mdnie = exynos4_mdnie_get(dev, "samsung,mdnie");
		if (IS_ERR(ctx->mdnie))
			return dev_err_probe(dev, PTR_ERR(ctx->mdnie),
					     "getting Samsung mDNIe pipeline\n");
		ret = devm_add_action_or_reset(dev, fimd_mdnie_put_action,
					       ctx->mdnie);
		if (ret)
			return ret;
	}

	ctx->bus_clk = devm_clk_get(dev, "fimd");
	if (IS_ERR(ctx->bus_clk)) {
		dev_err(dev, "failed to get bus clock\n");
		return PTR_ERR(ctx->bus_clk);
	}

	ctx->lcd_clk = devm_clk_get(dev, "sclk_fimd");
	if (IS_ERR(ctx->lcd_clk)) {
		dev_err(dev, "failed to get lcd clock\n");
		return PTR_ERR(ctx->lcd_clk);
	}

	ctx->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ctx->regs))
		return PTR_ERR(ctx->regs);

	ret = platform_get_irq_byname(pdev, ctx->i80_if ? "lcd_sys" : "vsync");
	if (ret < 0)
		return ret;

	ret = devm_request_irq(dev, ret, fimd_irq_handler, 0, "drm_fimd", ctx);
	if (ret) {
		dev_err(dev, "irq request failed.\n");
		return ret;
	}

	spin_lock_init(&ctx->fifo_irq_lock);
	atomic_set(&ctx->fifo_diag_active_vblanks, 0);
	if (fimd_fifo_one_shot && !ctx->i80_if && fimd_is_tab3(ctx)) {
		int fifo_irq = platform_get_irq_byname(pdev, "fifo");

		if (fifo_irq >= 0) {
			ret = devm_request_irq(dev, fifo_irq,
					fimd_fifo_one_shot_irq, 0,
					"drm_fimd_fifo_once", ctx);
			if (!ret)
				ctx->fifo_diag_registered = true;
		}
		if (!ctx->fifo_diag_registered)
			dev_warn(dev, "FIMD FIFO diagnostic IRQ not available (disabled)\n");
	}

	init_waitqueue_head(&ctx->wait_vsync_queue);
	atomic_set(&ctx->wait_vsync_event, 0);

	platform_set_drvdata(pdev, ctx);

	ctx->encoder = exynos_dpi_probe(dev);
	if (IS_ERR(ctx->encoder))
		return PTR_ERR(ctx->encoder);

	pm_runtime_enable(dev);

	ret = component_add(dev, &fimd_component_ops);
	if (ret)
		goto err_disable_pm_runtime;

	return ret;

err_disable_pm_runtime:
	pm_runtime_disable(dev);

	return ret;
}

static void fimd_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);

	component_del(&pdev->dev, &fimd_component_ops);
}

static int exynos_fimd_suspend(struct device *dev)
{
	struct fimd_context *ctx = dev_get_drvdata(dev);

	clk_disable_unprepare(ctx->lcd_clk);
	clk_disable_unprepare(ctx->bus_clk);

	return 0;
}

static int exynos_fimd_resume(struct device *dev)
{
	struct fimd_context *ctx = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(ctx->bus_clk);
	if (ret < 0) {
		DRM_DEV_ERROR(dev,
			      "Failed to prepare_enable the bus clk [%d]\n",
			      ret);
		return ret;
	}

	ret = clk_prepare_enable(ctx->lcd_clk);
	if  (ret < 0) {
		DRM_DEV_ERROR(dev,
			      "Failed to prepare_enable the lcd clk [%d]\n",
			      ret);
		clk_disable_unprepare(ctx->bus_clk);
		return ret;
	}

	return 0;
}

static DEFINE_RUNTIME_DEV_PM_OPS(exynos_fimd_pm_ops, exynos_fimd_suspend,
				 exynos_fimd_resume, NULL);

struct platform_driver fimd_driver = {
	.probe		= fimd_probe,
	.remove		= fimd_remove,
	.driver		= {
		.name	= "exynos4-fb",
		.pm	= pm_ptr(&exynos_fimd_pm_ops),
		.of_match_table = fimd_driver_dt_match,
	},
};
