/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __EXYNOS4_MDNIE_REGS_H__
#define __EXYNOS4_MDNIE_REGS_H__

#include <linux/bits.h>

#define MDNIE_REG_BANK                  0x0000
#define MDNIE_REG_INPUT_CONTROL         0x0001
#define MDNIE_REG_WIDTH                 0x0003
#define MDNIE_REG_HEIGHT                0x0004
#define MDNIE_REG_MASK                  0x00ff
#define MDNIE_REG_PWM_CONTROL           0x00b6
#define MDNIE_REG_POWER_LUT0            0x0079
#define MDNIE_REG_POWER_LUT2            0x007a
#define MDNIE_REG_POWER_LUT4            0x007b
#define MDNIE_REG_POWER_LUT6            0x007c
#define MDNIE_REG_POWER_LUT8            0x007d
#define MDNIE_REG_SCR_RED_R             0x00e1
#define MDNIE_REG_SCR_WHITE_B           0x00ec

#define MDNIE_INPUT_DATA_ENABLE         BIT(10)
#define MDNIE_SIZE_MASK                 GENMASK(10, 0)
#define MDNIE_MASK_ALL                  0x9fff
#define MDNIE_PWM_BANK                  1

#define IELCD_VIDCON0                   0x0000
#define IELCD_VIDCON1                   0x0004
#define IELCD_VIDTCON0                  0x0010
#define IELCD_VIDTCON1                  0x0014
#define IELCD_VIDTCON2                  0x0018
#define IELCD_WINCON0                   0x0020
#define IELCD_WINSHMAP                  0x0034
#define IELCD_VIDOSD0A                  0x0040
#define IELCD_VIDOSD0B                  0x0044
#define IELCD_VIDOSD0C                  0x0048
#define IELCD_GPOUTCON0                 0x0278
#define IELCD_MAGIC_KEY                 0x0002ff47

#define FIMD_VIDCON0                    0x0000
#define FIMD_VIDCON1                    0x0004
#define FIMD_VIDTCON0                   0x0010
#define FIMD_VIDTCON1                   0x0014
#define FIMD_VIDTCON2                   0x0018

#define VIDCON0_ENVID                   BIT(1)
#define VIDCON0_ENVID_F                 BIT(0)
#define VIDCON0_VIDOUT_MASK             GENMASK(28, 26)
#define VIDCON0_VCLKEN_MASK             BIT(5)
#define VIDCON1_POLARITY_MASK           GENMASK(7, 4)
#define VIDCON1_LINECNT_MASK            GENMASK(26, 16)
#define WINCON_DATAPATH_LOCAL           BIT(22)
#define WINCON_WSWP                     BIT(15)
#define WINCON_BPPMODE_32BPP            (0xd << 2)
#define WINCON_ENWIN                    BIT(0)
#define WINSHMAP_CH0_ENABLE             BIT(0)
#define VIDOSD_RIGHT_X(x)               (((x) & 0x7ff) << 11)
#define VIDOSD_BOTTOM_Y(y)              ((y) & 0x7ff)

#define FIMD_DUALRGB                    0x027c
#define FIMD_DUALRGB_BYPASS             0
#define FIMD_DUALRGB_MDNIE              3

#define LCD_BLK_CFG                     0x0210
#define LCD_BLK_MDNIE_ROUTE_MASK        (BIT(13) | BIT(12) | GENMASK(11, 10) | BIT(1) | BIT(0))
#define LCD_BLK_MDNIE_ROUTE_VALUE       BIT(0)
#define LCD_BLK_BYPASS_VALUE            BIT(1)

#endif
