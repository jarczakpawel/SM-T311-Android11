/*
 * SM-T311 / lt01 device-specific Samsung Light HAL configuration.
 *
 * Keep unsupported button and notification LED nodes undefined so the HAL
 * advertises only hardware that is actually present and wired in this port.
 */
#ifndef SM_T311_SAMSUNG_LIGHTS_H
#define SM_T311_SAMSUNG_LIGHTS_H

#define PANEL_BRIGHTNESS_NODE "/sys/class/backlight/lcd-bl/brightness"
#define PANEL_MAX_BRIGHTNESS_NODE "/sys/class/backlight/lcd-bl/max_brightness"

#endif  // SM_T311_SAMSUNG_LIGHTS_H
