#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_panel_io.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WS147_LCD_H_RES 172
#define WS147_LCD_V_RES 320
#define WS147_UI_HOR_RES 320
#define WS147_UI_VER_RES 172

esp_err_t waveshare_147_init(void);
esp_err_t waveshare_147_set_backlight(uint8_t percent);
esp_err_t waveshare_147_fill(uint16_t rgb565);
esp_err_t waveshare_147_draw_bitmap(int x_start, int y_start, int x_end, int y_end, const void *color_data);
esp_err_t waveshare_147_enable_landscape(void);

typedef void (*ws147_flush_done_cb_t)(void *ctx);
void waveshare_147_set_flush_done_callback(ws147_flush_done_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif
