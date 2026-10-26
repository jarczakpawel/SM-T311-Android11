/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __EXYNOS4_MDNIE_PRIV_H__
#define __EXYNOS4_MDNIE_PRIV_H__

#define MDNIE_END_SEQ 0xffff

enum mdnie_mode {
        MDNIE_DYNAMIC,
        MDNIE_STANDARD,
        MDNIE_MOVIE,
        MDNIE_AUTO,
        MDNIE_MODE_MAX,
};

enum mdnie_scenario {
        MDNIE_UI,
        MDNIE_VIDEO,
        MDNIE_VIDEO_WARM,
        MDNIE_VIDEO_COLD,
        MDNIE_CAMERA,
        MDNIE_NAVI,
        MDNIE_GALLERY,
        MDNIE_VT,
        MDNIE_BROWSER,
        MDNIE_EBOOK,
        MDNIE_SCENARIO_MAX,
        MDNIE_COLOR_TONE_1 = 40,
        MDNIE_COLOR_TONE_2,
        MDNIE_COLOR_TONE_3,
        MDNIE_COLOR_TONE_MAX,
};

enum mdnie_outdoor {
        MDNIE_OUTDOOR_OFF,
        MDNIE_OUTDOOR_ON,
        MDNIE_OUTDOOR_MAX,
};

enum mdnie_tone {
        MDNIE_TONE_NORMAL,
        MDNIE_TONE_WARM,
        MDNIE_TONE_COLD,
        MDNIE_TONE_MAX,
};

enum mdnie_cabc {
        MDNIE_CABC_OFF,
        MDNIE_CABC_ON,
        MDNIE_CABC_MAX,
};

enum mdnie_power_lut {
        MDNIE_LUT_DEFAULT,
        MDNIE_LUT_VIDEO,
        MDNIE_LUT_MAX,
};

enum mdnie_power_lut_level {
        MDNIE_LUT_INDOOR,
        MDNIE_LUT_OUTDOOR_1,
        MDNIE_LUT_OUTDOOR_2,
        MDNIE_LUT_LEVEL_MAX,
};

enum mdnie_accessibility {
        MDNIE_ACCESSIBILITY_OFF,
        MDNIE_ACCESSIBILITY_NEGATIVE,
        MDNIE_ACCESSIBILITY_COLOR_BLIND,
        MDNIE_ACCESSIBILITY_MAX,
};

struct mdnie_tuning_info {
        const char *name;
        unsigned short *sequence;
};

struct mdnie_backlight_value {
        unsigned int max;
        unsigned int mid;
        unsigned char low;
        unsigned char dim;
};

#endif
