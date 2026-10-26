/*
 * Samsung Galaxy Tab 3 8.0 (lt01 / SM-T311) Light HAL configuration.
 *
 * Advertise only the backlight implemented by the LP8556 kernel driver.
 */
#ifndef SM_T311_SAMSUNG_LIGHTS_H
#define SM_T311_SAMSUNG_LIGHTS_H

#define PANEL_BRIGHTNESS_NODE "/sys/class/backlight/lcd-bl/brightness"
#define PANEL_MAX_BRIGHTNESS_NODE "/sys/class/backlight/lcd-bl/max_brightness"

#endif  // SM_T311_SAMSUNG_LIGHTS_H
