#include "waveshare_147.hpp"

#include <stdbool.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "lcd_panel_st7789t.h"

// Waveshare ESP32-C6-LCD-1.47 pin mapping from vendor demo.
#define WS147_LCD_HOST SPI2_HOST
#define WS147_LCD_PCLK_HZ (12 * 1000 * 1000)
#define WS147_PIN_SCLK 7
#define WS147_PIN_MOSI 6
#define WS147_PIN_LCD_CS 14
#define WS147_PIN_LCD_DC 15
#define WS147_PIN_LCD_RST 21
#define WS147_PIN_BK_LIGHT 22
#define WS147_PIN_RGB_LED 8
#define WS147_LCD_CMD_BITS 8
#define WS147_LCD_PARAM_BITS 8

#define WS147_LEDC_TIMER LEDC_TIMER_0
#define WS147_LEDC_CHANNEL LEDC_CHANNEL_0
#define WS147_LEDC_MODE LEDC_LOW_SPEED_MODE
#define WS147_LEDC_DUTY_RES LEDC_TIMER_13_BIT
#define WS147_LEDC_MAX_DUTY ((1 << WS147_LEDC_DUTY_RES) - 1)

#define WS147_FILL_LINES 20

static const char *TAG = "ws147";
static esp_lcd_panel_handle_t s_panel_handle = NULL;
static esp_lcd_panel_io_handle_t s_io_handle = NULL;
static bool s_backlight_ready = false;
static uint16_t s_fill_buf[WS147_LCD_H_RES * WS147_FILL_LINES];
static ws147_flush_done_cb_t s_flush_done_cb = NULL;
static void *s_flush_done_ctx = NULL;

static bool ws147_color_trans_done_cb(esp_lcd_panel_io_handle_t panel_io,
                                      esp_lcd_panel_io_event_data_t *edata,
                                      void *user_ctx)
{
    (void)panel_io;
    (void)edata;
    (void)user_ctx;
    if (s_flush_done_cb) {
        s_flush_done_cb(s_flush_done_ctx);
    }
    return false;
}

static esp_err_t ws147_backlight_init(void)
{
    gpio_config_t bk_gpio_config = {};
    bk_gpio_config.pin_bit_mask = 1ULL << WS147_PIN_BK_LIGHT;
    bk_gpio_config.mode = GPIO_MODE_OUTPUT;
    bk_gpio_config.pull_up_en = GPIO_PULLUP_DISABLE;
    bk_gpio_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    bk_gpio_config.intr_type = GPIO_INTR_DISABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&bk_gpio_config), TAG, "configure backlight pin failed");

    ledc_timer_config_t ledc_timer = {};
    ledc_timer.speed_mode = WS147_LEDC_MODE;
    ledc_timer.timer_num = WS147_LEDC_TIMER;
    ledc_timer.duty_resolution = WS147_LEDC_DUTY_RES;
    ledc_timer.freq_hz = 5000;
    ledc_timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_RETURN_ON_ERROR(ledc_timer_config(&ledc_timer), TAG, "configure ledc timer failed");

    ledc_channel_config_t ledc_channel = {};
    ledc_channel.gpio_num = WS147_PIN_BK_LIGHT;
    ledc_channel.speed_mode = WS147_LEDC_MODE;
    ledc_channel.channel = WS147_LEDC_CHANNEL;
    ledc_channel.intr_type = LEDC_INTR_DISABLE;
    ledc_channel.timer_sel = WS147_LEDC_TIMER;
    ledc_channel.duty = 0;
    ledc_channel.hpoint = 0;
    ESP_RETURN_ON_ERROR(ledc_channel_config(&ledc_channel), TAG, "configure ledc channel failed");

    s_backlight_ready = true;
    return ESP_OK;
}

static esp_err_t ws147_rgb_led_off(void)
{
    gpio_config_t rgb_gpio_config = {};
    rgb_gpio_config.pin_bit_mask = 1ULL << WS147_PIN_RGB_LED;
    rgb_gpio_config.mode = GPIO_MODE_OUTPUT;
    rgb_gpio_config.pull_up_en = GPIO_PULLUP_DISABLE;
    rgb_gpio_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    rgb_gpio_config.intr_type = GPIO_INTR_DISABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&rgb_gpio_config), TAG, "configure rgb led pin failed");
    ESP_RETURN_ON_ERROR(gpio_set_level((gpio_num_t)WS147_PIN_RGB_LED, 0), TAG, "set rgb led off failed");
    return ESP_OK;
}

esp_err_t waveshare_147_set_backlight(uint8_t percent)
{
    if (!s_backlight_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (percent > 100) {
        percent = 100;
    }

    uint32_t duty = (WS147_LEDC_MAX_DUTY * percent) / 100;
    ESP_RETURN_ON_ERROR(ledc_set_duty(WS147_LEDC_MODE, WS147_LEDC_CHANNEL, duty), TAG, "set duty failed");
    ESP_RETURN_ON_ERROR(ledc_update_duty(WS147_LEDC_MODE, WS147_LEDC_CHANNEL), TAG, "update duty failed");
    return ESP_OK;
}

esp_err_t waveshare_147_init(void)
{
    if (s_panel_handle) {
        return ESP_OK;
    }

    spi_bus_config_t buscfg = {};
    buscfg.sclk_io_num = WS147_PIN_SCLK;
    buscfg.mosi_io_num = WS147_PIN_MOSI;
    buscfg.miso_io_num = -1;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = WS147_LCD_H_RES * WS147_FILL_LINES * sizeof(uint16_t);

    esp_err_t ret = spi_bus_initialize(WS147_LCD_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.dc_gpio_num = WS147_PIN_LCD_DC;
    io_config.cs_gpio_num = WS147_PIN_LCD_CS;
    io_config.pclk_hz = WS147_LCD_PCLK_HZ;
    io_config.lcd_cmd_bits = WS147_LCD_CMD_BITS;
    io_config.lcd_param_bits = WS147_LCD_PARAM_BITS;
    io_config.spi_mode = 0;
    io_config.trans_queue_depth = 10;
    io_config.on_color_trans_done = ws147_color_trans_done_cb;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)WS147_LCD_HOST, &io_config, &s_io_handle),
        TAG,
        "new panel io failed");

    esp_lcd_panel_dev_st7789t_config_t panel_config = {};
    panel_config.reset_gpio_num = WS147_PIN_LCD_RST;
    panel_config.rgb_endian = LCD_RGB_ENDIAN_BGR;
    panel_config.bits_per_pixel = 16;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789t(s_io_handle, &panel_config, &s_panel_handle), TAG, "new panel failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel_handle), TAG, "panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel_handle), TAG, "panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel_handle, true, false), TAG, "set default orientation failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel_handle, true), TAG, "display on failed");

    ESP_RETURN_ON_ERROR(ws147_rgb_led_off(), TAG, "disable rgb led failed");
    ESP_RETURN_ON_ERROR(ws147_backlight_init(), TAG, "backlight init failed");
    ESP_RETURN_ON_ERROR(waveshare_147_set_backlight(50), TAG, "set backlight failed");

    ESP_LOGI(TAG, "Waveshare 1.47 LCD initialized");
    return ESP_OK;
}

esp_err_t waveshare_147_fill(uint16_t rgb565)
{
    if (!s_panel_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    for (size_t i = 0; i < sizeof(s_fill_buf) / sizeof(s_fill_buf[0]); i++) {
        s_fill_buf[i] = rgb565;
    }

    for (int y = 0; y < WS147_LCD_V_RES; y += WS147_FILL_LINES) {
        int lines = WS147_FILL_LINES;
        if (y + lines > WS147_LCD_V_RES) {
            lines = WS147_LCD_V_RES - y;
        }
        ESP_RETURN_ON_ERROR(
            esp_lcd_panel_draw_bitmap(s_panel_handle, 0, y, WS147_LCD_H_RES, y + lines, s_fill_buf),
            TAG,
            "draw bitmap failed");
    }
    return ESP_OK;
}

esp_err_t waveshare_147_draw_bitmap(int x_start, int y_start, int x_end, int y_end, const void *color_data)
{
    if (!s_panel_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    if (x_start < 0 || y_start < 0 || x_end <= x_start || y_end <= y_start || color_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return esp_lcd_panel_draw_bitmap(s_panel_handle, x_start, y_start, x_end, y_end, color_data);
}

esp_err_t waveshare_147_enable_landscape(void)
{
    if (!s_panel_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel_handle, true), TAG, "swap_xy failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel_handle, true, true), TAG, "mirror failed");
    return ESP_OK;
}

void waveshare_147_set_flush_done_callback(ws147_flush_done_cb_t cb, void *ctx)
{
    s_flush_done_cb = cb;
    s_flush_done_ctx = ctx;
}
