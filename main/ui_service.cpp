#include "ui_service.hpp"

#include <stdio.h>

#include "lvgl.h"

#include "metric_catalog.hpp"
#include "state.hpp"

namespace {

lv_obj_t *s_title = nullptr;
lv_obj_t *s_value = nullptr;
lv_obj_t *s_status = nullptr;

void ensure_ui_created()
{
    if (s_title != nullptr) {
        return;
    }

    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    s_title = lv_label_create(screen);
    lv_label_set_text(s_title, "CENTER TEST");
    lv_obj_set_style_text_color(s_title, lv_palette_lighten(LV_PALETTE_GREY, 2), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 18);

    s_value = lv_label_create(screen);
    lv_label_set_text(s_value, "00:00");
    lv_obj_set_style_text_color(s_value, lv_palette_main(LV_PALETTE_CYAN), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_value, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_align(s_value, LV_ALIGN_CENTER, 0, 0);

    s_status = lv_label_create(screen);
    lv_obj_set_style_text_color(s_status, lv_palette_lighten(LV_PALETTE_GREY, 2), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(s_status, LV_ALIGN_BOTTOM_MID, 0, -18);
}

} // namespace

esp_err_t ui_service_init(AppState &state)
{
    ensure_ui_created();
    state.ui.ready = true;
    return ESP_OK;
}

void ui_service_refresh(AppState &state)
{
    ensure_ui_created();
    const MetricSpec &metric = selected_metric_spec(state);

    char status_text[64];
    snprintf(
        status_text,
        sizeof(status_text),
        "k:%s m:%02X b:%lu f:%lu",
        state.kline.status,
        state.kline.active_module,
        static_cast<unsigned long>(state.kline.baud),
        static_cast<unsigned long>(state.ui.frame_counter));
    lv_label_set_text(s_status, status_text);

    char title_text[64];
    snprintf(
        title_text,
        sizeof(title_text),
        "%s [%02X:%02X:%u] %s",
        metric.title,
        metric.module,
        metric.group,
        metric.measurement_index,
        state.kline.connected ? "OK" : "WAIT");
    lv_label_set_text(s_title, title_text);

    const MetricReading &reading = state.metrics[state.ui.selected_metric];
    char value_text[64];
    if (!reading.valid) {
        snprintf(value_text, sizeof(value_text), "--");
    } else if (reading.type == MetricValueType::Numeric) {
        if (reading.units[0] != '\0') {
            snprintf(value_text, sizeof(value_text), "%.1f %s", reading.numeric_value, reading.units);
        } else {
            snprintf(value_text, sizeof(value_text), "%.1f", reading.numeric_value);
        }
    } else if (reading.type == MetricValueType::Text) {
        snprintf(value_text, sizeof(value_text), "%s", reading.text);
    } else {
        snprintf(value_text, sizeof(value_text), "--");
    }
    lv_label_set_text(s_value, value_text);
}
