#include "lvgl_port.hpp"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "waveshare_147.hpp"

#define LVGL_TICK_PERIOD_MS 2
#define LVGL_BUF_LINES 20
#define WS147_OFFSET_X 0
#define WS147_OFFSET_Y 34

static const char *TAG = "lvgl_port";
static lv_disp_draw_buf_t s_disp_buf;
static lv_color_t s_buf1[WS147_LCD_V_RES * LVGL_BUF_LINES];
static lv_color_t s_buf2[WS147_LCD_V_RES * LVGL_BUF_LINES];
static lv_disp_drv_t s_disp_drv;
static esp_timer_handle_t s_lvgl_tick_timer = NULL;

static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void lvgl_flush_done_cb(void *ctx)
{
    lv_disp_drv_t *drv = static_cast<lv_disp_drv_t *>(ctx);
    lv_disp_flush_ready(drv);
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_err_t err = waveshare_147_draw_bitmap(
        area->x1 + WS147_OFFSET_X,
        area->y1 + WS147_OFFSET_Y,
        area->x2 + WS147_OFFSET_X + 1,
        area->y2 + WS147_OFFSET_Y + 1,
        color_map);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "flush failed: %s", esp_err_to_name(err));
        lv_disp_flush_ready(drv);
    }
}

esp_err_t lvgl_port_init(void)
{
    lv_init();

    ESP_RETURN_ON_ERROR(waveshare_147_enable_landscape(), TAG, "enable landscape failed");

    lv_disp_draw_buf_init(&s_disp_buf, s_buf1, s_buf2, WS147_LCD_V_RES * LVGL_BUF_LINES);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = WS147_UI_HOR_RES;
    s_disp_drv.ver_res = WS147_UI_VER_RES;
    s_disp_drv.flush_cb = lvgl_flush_cb;
    s_disp_drv.draw_buf = &s_disp_buf;
    lv_disp_drv_register(&s_disp_drv);
    waveshare_147_set_flush_done_callback(lvgl_flush_done_cb, &s_disp_drv);

    esp_timer_create_args_t lvgl_tick_timer_args = {};
    lvgl_tick_timer_args.callback = &lvgl_tick_cb;
    lvgl_tick_timer_args.name = "lvgl_tick";
    ESP_RETURN_ON_ERROR(esp_timer_create(&lvgl_tick_timer_args, &s_lvgl_tick_timer), TAG, "create tick timer failed");
    ESP_RETURN_ON_ERROR(
        esp_timer_start_periodic(s_lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000),
        TAG,
        "start tick timer failed");

    return ESP_OK;
}

void lvgl_port_task_handler(void)
{
    lv_timer_handler();
}

void lvgl_port_create_center_test_ui(const char *text)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *frame = lv_obj_create(screen);
    lv_obj_remove_style_all(frame);
    lv_obj_set_size(frame, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));
    lv_obj_center(frame);
    lv_obj_set_style_border_width(frame, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(frame, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(frame, LV_OPA_TRANSP, LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, text ? text : "CENTER");
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(label);
}
