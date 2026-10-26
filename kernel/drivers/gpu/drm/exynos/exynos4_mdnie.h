/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __EXYNOS4_MDNIE_H__
#define __EXYNOS4_MDNIE_H__

#include <linux/err.h>
#include <linux/kconfig.h>
#include <linux/types.h>

struct device;
struct exynos4_mdnie;

#if IS_ENABLED(CONFIG_DRM_EXYNOS4_MDNIE)
struct exynos4_mdnie *exynos4_mdnie_get(struct device *dev,
                                        const char *phandle_name);
void exynos4_mdnie_put(struct exynos4_mdnie *mdnie);
int exynos4_mdnie_prepare(struct exynos4_mdnie *mdnie, void __iomem *fimd_regs,
                          unsigned long pixel_rate);
int exynos4_mdnie_enable(struct exynos4_mdnie *mdnie);
int exynos4_mdnie_apply_tuning(struct exynos4_mdnie *mdnie);
void exynos4_mdnie_disable(struct exynos4_mdnie *mdnie);
void exynos4_mdnie_unprepare(struct exynos4_mdnie *mdnie);
#else
static inline struct exynos4_mdnie *
exynos4_mdnie_get(struct device *dev, const char *phandle_name)
{
        return ERR_PTR(-ENODEV);
}
static inline void exynos4_mdnie_put(struct exynos4_mdnie *mdnie) { }
static inline int exynos4_mdnie_prepare(struct exynos4_mdnie *mdnie,
                                        void __iomem *fimd_regs,
                                        unsigned long pixel_rate)
{
        return -ENODEV;
}
static inline int exynos4_mdnie_enable(struct exynos4_mdnie *mdnie)
{
        return -ENODEV;
}

static inline int exynos4_mdnie_apply_tuning(struct exynos4_mdnie *mdnie)
{
	return -ENODEV;
}

static inline void exynos4_mdnie_disable(struct exynos4_mdnie *mdnie) { }
static inline void exynos4_mdnie_unprepare(struct exynos4_mdnie *mdnie) { }
#endif

#endif
