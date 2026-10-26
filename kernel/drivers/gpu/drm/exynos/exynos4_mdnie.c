// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung Exynos4212/4412 mDNIe + IELCD integration.
 *
 * This is a DT/runtime-PM adaptation of Samsung's downstream display path:
 * FIMD -> mDNIe -> IELCD/LCD Lite -> DSIM -> panel.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/pwm.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#include "exynos4_mdnie.h"
#include "exynos4_mdnie_priv.h"
#include "exynos4_mdnie_regs.h"
#include "exynos4_mdnie_table_lt01.h"
#include "exynos4_mdnie_color_tone.h"

#define MDNIE_MAX_BRIGHTNESS            255
#define MDNIE_MID_BRIGHTNESS            150
#define MDNIE_LOW_BRIGHTNESS            30
#define MDNIE_DIM_BRIGHTNESS            20
#define MDNIE_CABC_OFF_BRIGHTNESS       19
#define MDNIE_FIXED_PWM_PERIOD_NS       116364ULL

struct exynos4_mdnie {
        struct device *dev;
        void __iomem *regs;
        void __iomem *ielcd;
        void __iomem *fimd_regs;
        struct regmap *sysreg;
        struct clk *gate_clk;
        struct clk *pixel_clk;
        struct clk *pwm_clk;
        struct clk *pwm_pre_clk;
        struct pwm_chip *pwm_chip;
        struct pwm_state pwm_state;
        struct mutex lock;
        struct mutex reg_lock;
        enum mdnie_mode mode;
        enum mdnie_scenario scenario;
        enum mdnie_outdoor outdoor;
        enum mdnie_tone tone;
        enum mdnie_cabc cabc;
        enum mdnie_accessibility accessibility;
        enum mdnie_power_lut_level power_lut_idx;
        unsigned int auto_brightness;
        bool negative;
        bool prepared;
        bool enabled;
        unsigned int width;
        unsigned int height;
        unsigned int last_brightness;
        unsigned long pixel_rate;
};

static inline u32 mdnie_read(struct exynos4_mdnie *mdnie, u16 reg)
{
        return readl(mdnie->regs + reg * 4);
}

static inline void mdnie_write(struct exynos4_mdnie *mdnie, u16 reg, u32 val)
{
        writel(val, mdnie->regs + reg * 4);
}

static int mdnie_send_sequence(struct exynos4_mdnie *mdnie,
                               const unsigned short *seq)
{
        unsigned int i;

        if (!seq)
                return -EINVAL;

        mutex_lock(&mdnie->reg_lock);
        mdnie_write(mdnie, MDNIE_REG_MASK, MDNIE_MASK_ALL);
        for (i = 0; seq[i] != MDNIE_END_SEQ; i += 2)
                mdnie_write(mdnie, seq[i], seq[i + 1]);
        mdnie_write(mdnie, MDNIE_REG_MASK, 0);
        mutex_unlock(&mdnie->reg_lock);

        return 0;
}

static struct mdnie_tuning_info *mdnie_select_main_table(struct exynos4_mdnie *mdnie)
{
        if (mdnie->negative)
                return &negative_table[mdnie->cabc];

        if (mdnie->accessibility > MDNIE_ACCESSIBILITY_OFF &&
            mdnie->accessibility < MDNIE_ACCESSIBILITY_MAX)
                return &accessibility_table[mdnie->cabc][mdnie->accessibility];

        if (mdnie->scenario >= MDNIE_COLOR_TONE_1 &&
            mdnie->scenario < MDNIE_COLOR_TONE_MAX)
                return &color_tone_table[mdnie->scenario - MDNIE_COLOR_TONE_1];

        if (mdnie->scenario == MDNIE_CAMERA)
                return &camera_table[mdnie->mode][mdnie->outdoor];

        if (mdnie->scenario < MDNIE_SCENARIO_MAX)
                return &tuning_table[mdnie->cabc][mdnie->mode][mdnie->scenario];

        return NULL;
}

static struct mdnie_tuning_info *mdnie_select_etc_table(struct exynos4_mdnie *mdnie)
{
        if (mdnie->scenario >= MDNIE_VIDEO &&
            mdnie->scenario <= MDNIE_VIDEO_COLD)
                mdnie->tone = mdnie->scenario - MDNIE_VIDEO;
        else
                mdnie->tone = MDNIE_TONE_NORMAL;

        return &etc_table[mdnie->cabc][mdnie->outdoor][mdnie->tone];
}

static int mdnie_apply_profile_locked(struct exynos4_mdnie *mdnie)
{
        struct mdnie_tuning_info *table;
        int ret = 0;

        if (!mdnie->prepared)
                return 0;

        table = mdnie_select_main_table(mdnie);
        if (table && table->sequence) {
                ret = mdnie_send_sequence(mdnie, table->sequence);
                if (ret)
                        return ret;
                dev_info(mdnie->dev, "TAB3_MDNIE_PROFILE main=%s\n", table->name);
        }

        if (mdnie->scenario >= MDNIE_VIDEO &&
            mdnie->scenario <= MDNIE_VIDEO_COLD) {
                table = mdnie_select_etc_table(mdnie);
                if (table && table->sequence) {
                        ret = mdnie_send_sequence(mdnie, table->sequence);
                        if (ret)
                                return ret;
                        dev_info(mdnie->dev, "TAB3_MDNIE_PROFILE etc=%s\n", table->name);
                }
        }

        return 0;
}

static unsigned int mdnie_stock_pwm_level(unsigned int brightness)
{
        const struct mdnie_backlight_value *pwm = &backlight_table[0];
        unsigned int value;

        if (brightness >= MDNIE_MID_BRIGHTNESS)
                value = (brightness - MDNIE_MID_BRIGHTNESS) *
                        (pwm->max - pwm->mid) /
                        (MDNIE_MAX_BRIGHTNESS - MDNIE_MID_BRIGHTNESS) + pwm->mid;
        else if (brightness >= MDNIE_LOW_BRIGHTNESS)
                value = (brightness - MDNIE_LOW_BRIGHTNESS) *
                        (pwm->mid - pwm->low) /
                        (MDNIE_MID_BRIGHTNESS - MDNIE_LOW_BRIGHTNESS) + pwm->low;
        else if (brightness >= MDNIE_DIM_BRIGHTNESS)
                value = (brightness - MDNIE_DIM_BRIGHTNESS) *
                        (pwm->low - pwm->dim) /
                        (MDNIE_LOW_BRIGHTNESS - MDNIE_DIM_BRIGHTNESS) + pwm->dim;
        else if (brightness)
                value = pwm->dim;
        else
                return 0;

        value = min(value, 1600U);
        return value < 16 ? 1 : value >> 4;
}

static void mdnie_program_pwm_locked(struct exynos4_mdnie *mdnie,
                                     unsigned int brightness)
{
        const unsigned char *lut;
        unsigned int value = mdnie_stock_pwm_level(brightness);
        unsigned int idx;
        u16 min_duty;
        u16 reg;

        mdnie_write(mdnie, MDNIE_REG_BANK, MDNIE_PWM_BANK);
        if (!brightness || !mdnie->pwm_state.enabled) {
                mdnie_write(mdnie, MDNIE_REG_PWM_CONTROL, 0xc000);
                mdnie_write(mdnie, MDNIE_REG_MASK, 0);
                return;
        }

        if (brightness <= MDNIE_CABC_OFF_BRIGHTNESS ||
            mdnie->cabc == MDNIE_CABC_OFF ||
            mdnie->scenario == MDNIE_CAMERA) {
                mdnie_write(mdnie, MDNIE_REG_PWM_CONTROL, 0xc000 | value);
                mdnie_write(mdnie, MDNIE_REG_MASK, 0);
                return;
        }

        idx = (mdnie->scenario >= MDNIE_VIDEO &&
               mdnie->scenario <= MDNIE_VIDEO_COLD) ?
                MDNIE_LUT_VIDEO : MDNIE_LUT_DEFAULT;
        lut = power_lut[mdnie->power_lut_idx][idx];
        min_duty = lut[7] * value / 100;
        if (min_duty < 4) {
                reg = 0xc000 | max(1U, value * lut[3] / 100);
        } else {
                mdnie_write(mdnie, MDNIE_REG_POWER_LUT0,
                            (lut[0] * value / 100) << 8 |
                            (lut[1] * value / 100));
                mdnie_write(mdnie, MDNIE_REG_POWER_LUT2,
                            (lut[2] * value / 100) << 8 |
                            (lut[3] * value / 100));
                mdnie_write(mdnie, MDNIE_REG_POWER_LUT4,
                            (lut[4] * value / 100) << 8 |
                            (lut[5] * value / 100));
                mdnie_write(mdnie, MDNIE_REG_POWER_LUT6,
                            (lut[6] * value / 100) << 8 |
                            (lut[7] * value / 100));
                mdnie_write(mdnie, MDNIE_REG_POWER_LUT8,
                            (lut[8] * value / 100) << 8);
                reg = 0x5000 | (value << 4);
        }
        mdnie_write(mdnie, MDNIE_REG_PWM_CONTROL, reg);
        mdnie_write(mdnie, MDNIE_REG_MASK, 0);
}

static void mdnie_apply_pwm_locked(struct exynos4_mdnie *mdnie)
{
        u64 brightness;

        if (!mdnie->pwm_state.period)
                brightness = 0;
        else
                brightness = div64_u64(mdnie->pwm_state.duty_cycle *
                                       MDNIE_MAX_BRIGHTNESS,
                                       mdnie->pwm_state.period);
        mdnie->last_brightness = min_t(u64, brightness, MDNIE_MAX_BRIGHTNESS);
        if (mdnie->prepared)
                mdnie_program_pwm_locked(mdnie, mdnie->last_brightness);
}

static int exynos4_mdnie_pwm_apply(struct pwm_chip *chip,
                                   struct pwm_device *pwm,
                                   const struct pwm_state *state)
{
        struct exynos4_mdnie *mdnie = pwmchip_get_drvdata(chip);

        if (state->polarity != PWM_POLARITY_NORMAL)
                return -EINVAL;

        mutex_lock(&mdnie->lock);
        mdnie->pwm_state = *state;
        mdnie_apply_pwm_locked(mdnie);
        mutex_unlock(&mdnie->lock);
        return 0;
}

static int exynos4_mdnie_pwm_get_state(struct pwm_chip *chip,
                                       struct pwm_device *pwm,
                                       struct pwm_state *state)
{
        struct exynos4_mdnie *mdnie = pwmchip_get_drvdata(chip);

        mutex_lock(&mdnie->lock);
        *state = mdnie->pwm_state;
        mutex_unlock(&mdnie->lock);
        return 0;
}

static const struct pwm_ops exynos4_mdnie_pwm_ops = {
        .apply = exynos4_mdnie_pwm_apply,
        .get_state = exynos4_mdnie_pwm_get_state,
};

static int mdnie_set_route(struct exynos4_mdnie *mdnie, bool enable)
{
        return regmap_update_bits(mdnie->sysreg, LCD_BLK_CFG,
                                  LCD_BLK_MDNIE_ROUTE_MASK,
                                  enable ? LCD_BLK_MDNIE_ROUTE_VALUE :
                                           LCD_BLK_BYPASS_VALUE);
}

static void mdnie_set_size_stock(struct exynos4_mdnie *mdnie,
                                 unsigned int width, unsigned int height)
{
        u32 val;

        /* Samsung downstream: bank 0, input data unmasked, RMW geometry. */
        mutex_lock(&mdnie->reg_lock);

        mdnie_write(mdnie, MDNIE_REG_BANK, 0);

        val = mdnie_read(mdnie, MDNIE_REG_INPUT_CONTROL);
        val &= ~MDNIE_INPUT_DATA_ENABLE;
        mdnie_write(mdnie, MDNIE_REG_INPUT_CONTROL, val);

        val = mdnie_read(mdnie, MDNIE_REG_WIDTH);
        val &= ~MDNIE_SIZE_MASK;
        val |= width & MDNIE_SIZE_MASK;
        mdnie_write(mdnie, MDNIE_REG_WIDTH, val);

        val = mdnie_read(mdnie, MDNIE_REG_HEIGHT);
        val &= ~MDNIE_SIZE_MASK;
        val |= height & MDNIE_SIZE_MASK;
        mdnie_write(mdnie, MDNIE_REG_HEIGHT, val);

        mdnie_write(mdnie, MDNIE_REG_MASK, 0);
        mdnie_read(mdnie, MDNIE_REG_MASK);

        mutex_unlock(&mdnie->reg_lock);
}

static void ielcd_init_from_fimd(struct exynos4_mdnie *mdnie,
                                 void __iomem *fimd_regs)
{
        u32 vidcon0, vidcon1, vidtcon2;

        vidtcon2 = readl(fimd_regs + FIMD_VIDTCON2);
        mdnie->width = (vidtcon2 & 0x7ff) + 1;
        mdnie->height = ((vidtcon2 >> 11) & 0x7ff) + 1;

        writel(IELCD_MAGIC_KEY, mdnie->ielcd + IELCD_GPOUTCON0);
        vidcon1 = readl(fimd_regs + FIMD_VIDCON1) & VIDCON1_POLARITY_MASK;
        writel(vidcon1, mdnie->ielcd + IELCD_VIDCON1);
        writel(readl(fimd_regs + FIMD_VIDTCON0),
               mdnie->ielcd + IELCD_VIDTCON0);
        writel(readl(fimd_regs + FIMD_VIDTCON1),
               mdnie->ielcd + IELCD_VIDTCON1);
        writel(vidtcon2, mdnie->ielcd + IELCD_VIDTCON2);

        vidcon0 = readl(fimd_regs + FIMD_VIDCON0);
        vidcon0 &= ~(VIDCON0_VIDOUT_MASK | VIDCON0_VCLKEN_MASK |
                     VIDCON0_ENVID | VIDCON0_ENVID_F);
        writel(vidcon0, mdnie->ielcd + IELCD_VIDCON0);

        writel(0, mdnie->ielcd + IELCD_VIDOSD0A);
        writel(VIDOSD_RIGHT_X(mdnie->width - 1) |
               VIDOSD_BOTTOM_Y(mdnie->height - 1),
               mdnie->ielcd + IELCD_VIDOSD0B);
        writel(mdnie->width * mdnie->height,
               mdnie->ielcd + IELCD_VIDOSD0C);
        /* Samsung ielcd_window_on(): shadow channel first, ENWIN second. */
        writel(WINCON_DATAPATH_LOCAL | WINCON_BPPMODE_32BPP,
               mdnie->ielcd + IELCD_WINCON0);
        writel(readl(mdnie->ielcd + IELCD_WINSHMAP) |
               WINSHMAP_CH0_ENABLE,
               mdnie->ielcd + IELCD_WINSHMAP);
        writel(readl(mdnie->ielcd + IELCD_WINCON0) | WINCON_ENWIN,
               mdnie->ielcd + IELCD_WINCON0);
        readl(mdnie->ielcd + IELCD_WINCON0);
}

struct exynos4_mdnie *exynos4_mdnie_get(struct device *dev,
                                        const char *phandle_name)
{
        struct device_node *node;
        struct platform_device *pdev;
        struct exynos4_mdnie *mdnie;

        node = of_parse_phandle(dev->of_node, phandle_name, 0);
        if (!node)
                return ERR_PTR(-ENODEV);
        pdev = of_find_device_by_node(node);
        of_node_put(node);
        if (!pdev)
                return ERR_PTR(-EPROBE_DEFER);
        mdnie = platform_get_drvdata(pdev);
        if (!mdnie) {
                put_device(&pdev->dev);
                return ERR_PTR(-EPROBE_DEFER);
        }
        return mdnie;
}
EXPORT_SYMBOL_GPL(exynos4_mdnie_get);

void exynos4_mdnie_put(struct exynos4_mdnie *mdnie)
{
        if (mdnie)
                put_device(mdnie->dev);
}
EXPORT_SYMBOL_GPL(exynos4_mdnie_put);

int exynos4_mdnie_prepare(struct exynos4_mdnie *mdnie, void __iomem *fimd_regs,
                          unsigned long pixel_rate)
{
        u32 vidtcon2;
        int ret;

        if (!mdnie || !fimd_regs)
                return -EINVAL;

        mutex_lock(&mdnie->lock);
        if (mdnie->prepared) {
                mutex_unlock(&mdnie->lock);
                return 0;
        }

        ret = pm_runtime_resume_and_get(mdnie->dev);
        if (ret < 0)
                goto out_unlock;

        /* Samsung enables the mDNIe gate, then its source clock. */
        ret = clk_prepare_enable(mdnie->gate_clk);
        if (ret)
                goto err_pm;

        if (pixel_rate) {
                ret = clk_set_rate(mdnie->pixel_clk, pixel_rate);
                if (ret)
                        goto err_gate;
        }
        ret = clk_prepare_enable(mdnie->pixel_clk);
        if (ret)
                goto err_gate;

        /* LP8556 stock path: xusbxti -> 2.2 MHz PWM and 22 MHz pre-clock. */
        ret = clk_set_rate(mdnie->pwm_clk, 2200000);
        if (ret)
                goto err_pixel;
        ret = clk_set_rate(mdnie->pwm_pre_clk, 22000000);
        if (ret)
                goto err_pixel;
        ret = clk_prepare_enable(mdnie->pwm_clk);
        if (ret)
                goto err_pixel;

        ret = mdnie_set_route(mdnie, true);
        if (ret)
                goto err_pwm;

        mdnie->fimd_regs = fimd_regs;
        vidtcon2 = readl(fimd_regs + FIMD_VIDTCON2);
        mdnie->width = (vidtcon2 & 0x7ff) + 1;
        mdnie->height = ((vidtcon2 >> 11) & 0x7ff) + 1;
        mdnie->pixel_rate = clk_get_rate(mdnie->pixel_clk);
        mdnie->prepared = true;

        dev_info(mdnie->dev,
                 "TAB3_V138_MDNIE_CLOCKS_AND_ROUTE_READY width=%u height=%u pixel_clk=%lu pwm_pre_clk=%lu pwm_clk=%lu\n",
                 mdnie->width, mdnie->height, mdnie->pixel_rate,
                 clk_get_rate(mdnie->pwm_pre_clk),
                 clk_get_rate(mdnie->pwm_clk));

        mutex_unlock(&mdnie->lock);
        return 0;

err_pwm:
        clk_disable_unprepare(mdnie->pwm_clk);
err_pixel:
        clk_disable_unprepare(mdnie->pixel_clk);
err_gate:
        clk_disable_unprepare(mdnie->gate_clk);
err_pm:
        pm_runtime_put_sync(mdnie->dev);
out_unlock:
        mutex_unlock(&mdnie->lock);
        return ret;
}
EXPORT_SYMBOL_GPL(exynos4_mdnie_prepare);

int exynos4_mdnie_enable(struct exynos4_mdnie *mdnie)
{
	u32 val;
	u32 width_reg;
	u32 height_reg;
	int ret;

	if (!mdnie)
		return -EINVAL;

	mutex_lock(&mdnie->lock);
	if (!mdnie->prepared || !mdnie->fimd_regs) {
		ret = -EPIPE;
		goto out_unlock;
	}
	if (mdnie->enabled) {
		ret = 0;
		goto out_unlock;
	}

	/*
	 * Samsung boot order is split between the framebuffer and mDNIe
	 * platform drivers. The framebuffer driver first establishes the
	 * untuned transport path:
	 *
	 *   geometry -> IELCD global state -> IELCD ENVID
	 *
	 * The default FIMD window and FIMD ENVID are enabled afterwards.
	 * Only then does the separately registered mDNIe driver load the
	 * LT01 profile. Keep profile and PWM programming out of this
	 * transport phase to preserve the downstream register ordering.
	 */
	mdnie_set_size_stock(mdnie, mdnie->width, mdnie->height);
	ielcd_init_from_fimd(mdnie, mdnie->fimd_regs);

	val = readl(mdnie->ielcd + IELCD_VIDCON0);
	val |= VIDCON0_ENVID | VIDCON0_ENVID_F;
	writel(val, mdnie->ielcd + IELCD_VIDCON0);
	readl(mdnie->ielcd + IELCD_VIDCON0);
	mdnie->enabled = true;

	mutex_lock(&mdnie->reg_lock);
	mdnie_write(mdnie, MDNIE_REG_BANK, 0);
	width_reg = mdnie_read(mdnie, MDNIE_REG_WIDTH);
	height_reg = mdnie_read(mdnie, MDNIE_REG_HEIGHT);
	mutex_unlock(&mdnie->reg_lock);

	dev_info(mdnie->dev,
		 "TAB3_V141_STOCK_TRANSPORT_ON width=%u height=%u width_reg=%08x width_decoded=%u height_reg=%08x height_decoded=%u ielcd_vidcon0=%08x\n",
		 mdnie->width, mdnie->height, width_reg,
		 width_reg & (u32)MDNIE_SIZE_MASK, height_reg,
		 height_reg & (u32)MDNIE_SIZE_MASK,
		 readl(mdnie->ielcd + IELCD_VIDCON0));
	ret = 0;

out_unlock:
	mutex_unlock(&mdnie->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(exynos4_mdnie_enable);

int exynos4_mdnie_apply_tuning(struct exynos4_mdnie *mdnie)
{
	int ret;

	if (!mdnie)
		return -EINVAL;

	mutex_lock(&mdnie->lock);
	if (!mdnie->prepared || !mdnie->enabled) {
		ret = -EPIPE;
		goto out_unlock;
	}

	/* Stock mdnie_probe() runs after FIMD has already started scanout. */
	ret = mdnie_apply_profile_locked(mdnie);
	if (!ret)
		mdnie_apply_pwm_locked(mdnie);
	if (!ret)
		dev_info(mdnie->dev,
			 "TAB3_V141_STOCK_TUNING_AFTER_SCANOUT profile=1 pwm=1\n");

out_unlock:
	mutex_unlock(&mdnie->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(exynos4_mdnie_apply_tuning);

void exynos4_mdnie_disable(struct exynos4_mdnie *mdnie)
{
        u32 val;
        unsigned int timeout;

        if (!mdnie)
                return;
        mutex_lock(&mdnie->lock);
        if (mdnie->enabled) {
                val = readl(mdnie->ielcd + IELCD_VIDCON0);
                val |= VIDCON0_ENVID;
                val &= ~VIDCON0_ENVID_F;
                writel(val, mdnie->ielcd + IELCD_VIDCON0);
                for (timeout = 0; timeout < 2000000; timeout++) {
                        if (!(readl(mdnie->ielcd + IELCD_VIDCON1) &
                              VIDCON1_LINECNT_MASK))
                                break;
                        cpu_relax();
                }
                writel(0, mdnie->ielcd + IELCD_VIDCON0);
                mdnie->enabled = false;
                dev_info(mdnie->dev,
                         "TAB3_V138_MDNIE_IELCD_DISABLED timeout=%u\n",
                         timeout == 2000000);
        }
        mutex_unlock(&mdnie->lock);
}
EXPORT_SYMBOL_GPL(exynos4_mdnie_disable);

void exynos4_mdnie_unprepare(struct exynos4_mdnie *mdnie)
{
        if (!mdnie)
                return;
        mutex_lock(&mdnie->lock);
        if (mdnie->prepared) {
                mdnie_program_pwm_locked(mdnie, 0);
                mdnie_set_route(mdnie, false);
                clk_disable_unprepare(mdnie->pwm_clk);
                clk_disable_unprepare(mdnie->pixel_clk);
                clk_disable_unprepare(mdnie->gate_clk);
                pm_runtime_put_sync(mdnie->dev);
                mdnie->enabled = false;
                mdnie->prepared = false;
                mdnie->fimd_regs = NULL;
                dev_info(mdnie->dev,
                         "TAB3_V138_MDNIE_UNPREPARED route=bypass\n");
        }
        mutex_unlock(&mdnie->lock);
}
EXPORT_SYMBOL_GPL(exynos4_mdnie_unprepare);



#define MDNIE_ATTR_RW(_name, _field, _max)                                      \
static ssize_t _name##_show(struct device *dev,                                 \
                            struct device_attribute *attr, char *buf)            \
{                                                                                \
        struct exynos4_mdnie *mdnie = dev_get_drvdata(dev);                      \
        unsigned int value;                                                       \
        mutex_lock(&mdnie->lock);                                                 \
        value = mdnie->_field;                                                    \
        mutex_unlock(&mdnie->lock);                                               \
        return sysfs_emit(buf, "%u\\n", value);                              \
}                                                                                \
static ssize_t _name##_store(struct device *dev,                                \
                             struct device_attribute *attr,                      \
                             const char *buf, size_t count)                      \
{                                                                                \
        struct exynos4_mdnie *mdnie = dev_get_drvdata(dev);                      \
        unsigned int value;                                                       \
        int ret = kstrtouint(buf, 0, &value);                                    \
        if (ret)                                                                  \
                return ret;                                                       \
        if (value >= (_max))                                                       \
                return -EINVAL;                                                   \
        mutex_lock(&mdnie->lock);                                                 \
        mdnie->_field = value;                                                    \
        ret = mdnie_apply_profile_locked(mdnie);                                  \
        mdnie_apply_pwm_locked(mdnie);                                            \
        mutex_unlock(&mdnie->lock);                                               \
        return ret ? ret : count;                                                 \
}                                                                                \
static DEVICE_ATTR_RW(_name)

MDNIE_ATTR_RW(mode, mode, MDNIE_MODE_MAX);
MDNIE_ATTR_RW(scenario, scenario, MDNIE_COLOR_TONE_MAX);
MDNIE_ATTR_RW(outdoor, outdoor, MDNIE_OUTDOOR_MAX);
MDNIE_ATTR_RW(cabc, cabc, MDNIE_CABC_MAX);


static ssize_t accessibility_show(struct device *dev,
                                  struct device_attribute *attr, char *buf)
{
        struct exynos4_mdnie *mdnie = dev_get_drvdata(dev);
        unsigned short *seq;
        ssize_t len = 0;
        unsigned int i;

        mutex_lock(&mdnie->lock);
        len += sysfs_emit_at(buf, len, "%u", mdnie->accessibility);
        if (mdnie->accessibility == MDNIE_ACCESSIBILITY_COLOR_BLIND) {
                seq = accessibility_table[mdnie->cabc]
                                         [MDNIE_ACCESSIBILITY_COLOR_BLIND].sequence;
                for (i = 0; seq && seq[i] != MDNIE_END_SEQ; i += 2)
                        if (seq[i] >= MDNIE_REG_SCR_RED_R &&
                            seq[i] < MDNIE_REG_SCR_RED_R + 9)
                                len += sysfs_emit_at(buf, len, " 0x%04x",
                                                     seq[i + 1]);
        }
        len += sysfs_emit_at(buf, len, "\n");
        mutex_unlock(&mdnie->lock);
        return len;
}

static ssize_t accessibility_store(struct device *dev,
                                   struct device_attribute *attr,
                                   const char *buf, size_t count)
{
        struct exynos4_mdnie *mdnie = dev_get_drvdata(dev);
        unsigned int value, scr[9], cabc, i;
        unsigned short *seq;
        int parsed, ret;

        parsed = sscanf(buf, "%u %x %x %x %x %x %x %x %x %x",
                        &value, &scr[0], &scr[1], &scr[2], &scr[3],
                        &scr[4], &scr[5], &scr[6], &scr[7], &scr[8]);
        if (parsed < 1 || value >= MDNIE_ACCESSIBILITY_MAX)
                return -EINVAL;
        if (value == MDNIE_ACCESSIBILITY_COLOR_BLIND && parsed != 10)
                return -EINVAL;

        mutex_lock(&mdnie->lock);
        mdnie->accessibility = value;
        if (value == MDNIE_ACCESSIBILITY_COLOR_BLIND) {
                for (cabc = 0; cabc < MDNIE_CABC_MAX; cabc++) {
                        seq = accessibility_table[cabc]
                                                 [MDNIE_ACCESSIBILITY_COLOR_BLIND].sequence;
                        for (i = 0; seq && seq[i] != MDNIE_END_SEQ; i += 2)
                                if (seq[i] >= MDNIE_REG_SCR_RED_R &&
                                    seq[i] < MDNIE_REG_SCR_RED_R + 9)
                                        seq[i + 1] = scr[seq[i] - MDNIE_REG_SCR_RED_R];
                }
        }
        ret = mdnie_apply_profile_locked(mdnie);
        mutex_unlock(&mdnie->lock);
        return ret ? ret : count;
}
static DEVICE_ATTR_RW(accessibility);

static ssize_t auto_brightness_show(struct device *dev,
                                    struct device_attribute *attr, char *buf)
{
        struct exynos4_mdnie *mdnie = dev_get_drvdata(dev);

        return sysfs_emit(buf, "%u %u\n", mdnie->auto_brightness,
                          mdnie->power_lut_idx);
}

static ssize_t auto_brightness_store(struct device *dev,
                                     struct device_attribute *attr,
                                     const char *buf, size_t count)
{
        struct exynos4_mdnie *mdnie = dev_get_drvdata(dev);
        unsigned int value;
        int ret;

        ret = kstrtouint(buf, 0, &value);
        if (ret)
                return ret;

        mutex_lock(&mdnie->lock);
        mdnie->auto_brightness = value;
        mdnie->cabc = value ? MDNIE_CABC_ON : MDNIE_CABC_OFF;
        if (value >= 5)
                mdnie->power_lut_idx = MDNIE_LUT_OUTDOOR_2;
        else if (value >= 3)
                mdnie->power_lut_idx = MDNIE_LUT_OUTDOOR_1;
        else
                mdnie->power_lut_idx = MDNIE_LUT_INDOOR;
        ret = mdnie_apply_profile_locked(mdnie);
        mdnie_apply_pwm_locked(mdnie);
        mutex_unlock(&mdnie->lock);
        return ret ? ret : count;
}
static DEVICE_ATTR_RW(auto_brightness);

static ssize_t negative_show(struct device *dev,
                             struct device_attribute *attr, char *buf)
{
        struct exynos4_mdnie *mdnie = dev_get_drvdata(dev);
        return sysfs_emit(buf, "%u\n", mdnie->negative);
}

static ssize_t negative_store(struct device *dev,
                              struct device_attribute *attr,
                              const char *buf, size_t count)
{
        struct exynos4_mdnie *mdnie = dev_get_drvdata(dev);
        bool value;
        int ret = kstrtobool(buf, &value);

        if (ret)
                return ret;
        mutex_lock(&mdnie->lock);
        mdnie->negative = value;
        ret = mdnie_apply_profile_locked(mdnie);
        mutex_unlock(&mdnie->lock);
        return ret ? ret : count;
}
static DEVICE_ATTR_RW(negative);

static ssize_t state_show(struct device *dev,
                          struct device_attribute *attr, char *buf)
{
        struct exynos4_mdnie *mdnie = dev_get_drvdata(dev);
        u32 route = 0;

        regmap_read(mdnie->sysreg, LCD_BLK_CFG, &route);
        return sysfs_emit(buf,
                          "prepared=%u enabled=%u width=%u height=%u route=%08x mode=%u scenario=%u outdoor=%u cabc=%u negative=%u accessibility=%u brightness=%u\n",
                          mdnie->prepared, mdnie->enabled, mdnie->width,
                          mdnie->height, route, mdnie->mode, mdnie->scenario,
                          mdnie->outdoor, mdnie->cabc, mdnie->negative,
                          mdnie->accessibility, mdnie->last_brightness);
}
static DEVICE_ATTR_RO(state);

static struct attribute *exynos4_mdnie_attrs[] = {
        &dev_attr_mode.attr,
        &dev_attr_scenario.attr,
        &dev_attr_outdoor.attr,
        &dev_attr_cabc.attr,
        &dev_attr_negative.attr,
        &dev_attr_accessibility.attr,
        &dev_attr_auto_brightness.attr,
        &dev_attr_state.attr,
        NULL,
};
ATTRIBUTE_GROUPS(exynos4_mdnie);

static int exynos4_mdnie_probe(struct platform_device *pdev)
{
        struct device *dev = &pdev->dev;
        struct exynos4_mdnie *mdnie;
        struct pwm_chip *chip;
        int ret;

        chip = devm_pwmchip_alloc(dev, 1, sizeof(*mdnie));
        if (IS_ERR(chip))
                return PTR_ERR(chip);
        mdnie = pwmchip_get_drvdata(chip);
        mdnie->dev = dev;
        mdnie->pwm_chip = chip;
        mdnie->regs = devm_platform_ioremap_resource(pdev, 0);
        if (IS_ERR(mdnie->regs))
                return PTR_ERR(mdnie->regs);
        mdnie->ielcd = devm_platform_ioremap_resource(pdev, 1);
        if (IS_ERR(mdnie->ielcd))
                return PTR_ERR(mdnie->ielcd);
        mdnie->sysreg = syscon_regmap_lookup_by_phandle(dev->of_node,
                                                        "samsung,sysreg");
        if (IS_ERR(mdnie->sysreg))
                return dev_err_probe(dev, PTR_ERR(mdnie->sysreg),
                                     "getting sysreg\n");
        mdnie->gate_clk = devm_clk_get(dev, "mdnie");
        if (IS_ERR(mdnie->gate_clk))
                return dev_err_probe(dev, PTR_ERR(mdnie->gate_clk),
                                     "getting mdnie gate clock\n");
        mdnie->pixel_clk = devm_clk_get(dev, "sclk_mdnie");
        if (IS_ERR(mdnie->pixel_clk))
                return dev_err_probe(dev, PTR_ERR(mdnie->pixel_clk),
                                     "getting mdnie pixel clock\n");
        mdnie->pwm_clk = devm_clk_get(dev, "sclk_mdnie_pwm");
        if (IS_ERR(mdnie->pwm_clk))
                return dev_err_probe(dev, PTR_ERR(mdnie->pwm_clk),
                                     "getting mdnie PWM clock\n");
        mdnie->pwm_pre_clk = devm_clk_get(dev, "sclk_mdnie_pwm_pre");
        if (IS_ERR(mdnie->pwm_pre_clk))
                return dev_err_probe(dev, PTR_ERR(mdnie->pwm_pre_clk),
                                     "getting mdnie PWM pre-divider clock\n");

        mutex_init(&mdnie->lock);
        mutex_init(&mdnie->reg_lock);
        mdnie->mode = MDNIE_STANDARD;
        mdnie->scenario = MDNIE_UI;
        mdnie->outdoor = MDNIE_OUTDOOR_OFF;
        mdnie->cabc = MDNIE_CABC_OFF;
        mdnie->accessibility = MDNIE_ACCESSIBILITY_OFF;
        mdnie->power_lut_idx = MDNIE_LUT_INDOOR;
        mdnie->pwm_state.period = MDNIE_FIXED_PWM_PERIOD_NS;
        mdnie->pwm_state.duty_cycle = div_u64(MDNIE_FIXED_PWM_PERIOD_NS * 120, 255);
        mdnie->pwm_state.polarity = PWM_POLARITY_NORMAL;
        mdnie->pwm_state.enabled = false;

        chip->ops = &exynos4_mdnie_pwm_ops;
        chip->of_xlate = of_pwm_xlate_with_flags;
        ret = devm_pwmchip_add(dev, chip);
        if (ret)
                return dev_err_probe(dev, ret, "registering PWM provider\n");

        platform_set_drvdata(pdev, mdnie);
        pm_runtime_enable(dev);
        dev_info(dev,
                 "TAB3_V138_FULL_STOCK_MDNIE_PROBE mdnie=11ca0000 ielcd=11c40000 tables=lt01 pwm=cabc clocks=stock-parents\n");
        return 0;
}

static void exynos4_mdnie_remove(struct platform_device *pdev)
{
        struct exynos4_mdnie *mdnie = platform_get_drvdata(pdev);

        exynos4_mdnie_disable(mdnie);
        exynos4_mdnie_unprepare(mdnie);
        pm_runtime_disable(&pdev->dev);
}


static void exynos4_mdnie_shutdown(struct platform_device *pdev)
{
        struct exynos4_mdnie *mdnie = platform_get_drvdata(pdev);

        exynos4_mdnie_disable(mdnie);
        exynos4_mdnie_unprepare(mdnie);
}

static const struct of_device_id exynos4_mdnie_of_match[] = {
        { .compatible = "samsung,exynos4212-mdnie" },
        { .compatible = "samsung,exynos4412-mdnie" },
        { }
};
MODULE_DEVICE_TABLE(of, exynos4_mdnie_of_match);

static struct platform_driver exynos4_mdnie_driver = {
        .probe = exynos4_mdnie_probe,
        .remove = exynos4_mdnie_remove,
        .shutdown = exynos4_mdnie_shutdown,
        .driver = {
                .name = "exynos4-mdnie",
                .of_match_table = exynos4_mdnie_of_match,
                .dev_groups = exynos4_mdnie_groups,
        },
};
module_platform_driver(exynos4_mdnie_driver);

MODULE_AUTHOR("Samsung Electronics / SM-T31x mainline port");
MODULE_DESCRIPTION("Exynos4212/4412 full mDNIe + IELCD display pipeline");
MODULE_LICENSE("GPL");
