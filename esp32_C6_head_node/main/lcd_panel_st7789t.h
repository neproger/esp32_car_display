#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "esp_lcd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int reset_gpio_num;
    union {
        lcd_color_rgb_endian_t color_space;
        lcd_color_rgb_endian_t rgb_endian;
    };
    unsigned int bits_per_pixel;
    struct {
        unsigned int reset_active_high : 1;
    } flags;
    void *vendor_config;
} esp_lcd_panel_dev_st7789t_config_t;

esp_err_t esp_lcd_new_panel_st7789t(
    const esp_lcd_panel_io_handle_t io,
    const esp_lcd_panel_dev_st7789t_config_t *panel_dev_config,
    esp_lcd_panel_handle_t *ret_panel
);

#ifdef __cplusplus
}
#endif
