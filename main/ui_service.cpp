#include "ui_service.hpp"

#include <stdio.h>

#include "lvgl.h"

#include "metric_catalog.hpp"
#include "state.hpp"

namespace {

lv_obj_t *s_title = nullptr;
lv_obj_t *s_value = nullptr;
lv_obj_t *s_suffix = nullptr;
lv_obj_t *s_status = nullptr;
lv_obj_t *s_cpu_temp = nullptr;

void apply_metric_component(const MetricSpec &metric)
{
    if (metric.component == MetricComponentType::TextOnly) {
        lv_obj_add_flag(s_suffix, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(s_value, LV_ALIGN_CENTER, 0, 0);
        return;
    }

    lv_obj_clear_flag(s_suffix, LV_OBJ_FLAG_HIDDEN);

    switch (metric.layout) {
    case MetricLayout::Temperature:
        lv_obj_align(s_value, LV_ALIGN_CENTER, -18, 0);
        lv_obj_align(s_suffix, LV_ALIGN_CENTER, 74, -10);
        break;
    case MetricLayout::Speed:
        lv_obj_align(s_value, LV_ALIGN_CENTER, -10, 0);
        lv_obj_align(s_suffix, LV_ALIGN_CENTER, 84, -10);
        break;
    case MetricLayout::Rpm:
        lv_obj_align(s_value, LV_ALIGN_CENTER, -8, 0);
        lv_obj_align(s_suffix, LV_ALIGN_CENTER, 92, -10);
        break;
    default:
        lv_obj_align(s_value, LV_ALIGN_CENTER, -10, 0);
        lv_obj_align(s_suffix, LV_ALIGN_CENTER, 82, -10);
        break;
    }
}

void format_metric_value(const MetricSpec &metric, const MetricReading &reading, char *value_text, size_t value_text_size)
{
    if (!reading.valid) {
        snprintf(value_text, value_text_size, "--");
        return;
    }

    if (reading.type == MetricValueType::Text || metric.component == MetricComponentType::TextOnly) {
        snprintf(value_text, value_text_size, "%s", reading.text);
        return;
    }

    const float value = reading.numeric_value;
    switch (metric.format) {
    case MetricFormat::Rpm:
        snprintf(value_text, value_text_size, "%.0f", value);
        return;
    case MetricFormat::TemperatureC:
    case MetricFormat::SpeedKmh:
    case MetricFormat::Voltage:
    case MetricFormat::Generic:
        switch (metric.decimals) {
        case 0:
            snprintf(value_text, value_text_size, "%.0f", value);
            break;
        case 1:
            snprintf(value_text, value_text_size, "%.1f", value);
            break;
        default:
            snprintf(value_text, value_text_size, "%.2f", value);
            break;
        }
        return;
    case MetricFormat::Text:
        snprintf(value_text, value_text_size, "%s", reading.text);
        return;
    }
}

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

    s_suffix = lv_label_create(screen);
    lv_label_set_text(s_suffix, "");
    lv_obj_set_style_text_color(s_suffix, lv_palette_lighten(LV_PALETTE_GREY, 2), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_suffix, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(s_suffix, LV_ALIGN_CENTER, 84, -10);

    s_status = lv_label_create(screen);
    lv_obj_set_style_text_color(s_status, lv_palette_lighten(LV_PALETTE_GREY, 2), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(s_status, LV_ALIGN_BOTTOM_MID, 0, -18);

    s_cpu_temp = lv_label_create(screen);
    lv_obj_set_style_text_color(s_cpu_temp, lv_palette_lighten(LV_PALETTE_ORANGE, 1), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_cpu_temp, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(s_cpu_temp, LV_ALIGN_BOTTOM_RIGHT, -16, -48);
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
        "K %s  %02X  %lu",
        state.kline.status,
        state.kline.active_module,
        static_cast<unsigned long>(state.kline.baud));
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
    apply_metric_component(metric);
    format_metric_value(metric, reading, value_text, sizeof(value_text));
    lv_label_set_text(s_value, value_text);

    if (metric.component == MetricComponentType::NumericWithSuffix) {
        lv_label_set_text(s_suffix, metric.suffix != nullptr ? metric.suffix : "");
    } else {
        lv_label_set_text(s_suffix, "");
    }

    char cpu_temp_text[32];
    if (state.device.cpu_temp_valid) {
        snprintf(cpu_temp_text, sizeof(cpu_temp_text), "CPU %.1f C", state.device.cpu_temp_c);
    } else {
        snprintf(cpu_temp_text, sizeof(cpu_temp_text), "CPU --");
    }
    lv_label_set_text(s_cpu_temp, cpu_temp_text);
}
