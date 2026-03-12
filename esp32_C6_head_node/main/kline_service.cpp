#include "kline_service.hpp"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"

#include "KLineKWP1281Lib_ESP32.h"

#include "kline_uart.hpp"
#include "metric_catalog.hpp"
#include "state.hpp"

namespace {

static const char *TAG = "kline_service";

constexpr KlineUartConfig make_default_kline_config()
{
    return KlineUartConfig{UART_NUM_1, 16, 17, 256};
}

constexpr KlineUartConfig kDefaultKlineConfig = make_default_kline_config();
constexpr uint8_t kDefaultModule = 0x01;
constexpr unsigned long kBaudCandidates[] = {10400, 9600, 4800};

KLineKWP1281Lib s_kwp(
    kline_uart_begin,
    kline_uart_end,
    kline_uart_send,
    kline_uart_receive,
    static_cast<uint8_t>(kDefaultKlineConfig.tx_pin),
    true);

AppState *s_state = nullptr;

void clear_metric_reading(MetricReading &reading)
{
    reading.valid = false;
    reading.type = MetricValueType::Empty;
    reading.updated_at_ms = 0;
    reading.numeric_value = 0.0f;
    reading.units[0] = '\0';
    reading.text[0] = '\0';
}

void log_metric_update_if_changed(const MetricSpec &metric, const MetricReading &previous, const MetricReading &current)
{
    if (!current.valid) {
        return;
    }

    if (current.type != previous.type || previous.valid != current.valid) {
        // fall through to log below
    } else if (current.type == MetricValueType::Numeric) {
        if (strcmp(previous.units, current.units) == 0 && previous.numeric_value == current.numeric_value) {
            return;
        }
    } else if (current.type == MetricValueType::Text) {
        if (strcmp(previous.text, current.text) == 0) {
            return;
        }
    } else {
        return;
    }

    if (current.type == MetricValueType::Numeric) {
        ESP_LOGI(
            TAG,
            "ECU %02X grp %02X idx %u -> %.1f %s",
            metric.module,
            metric.group,
            metric.measurement_index,
            current.numeric_value,
            current.units);
    } else if (current.type == MetricValueType::Text) {
        ESP_LOGI(
            TAG,
            "ECU %02X grp %02X idx %u -> %s",
            metric.module,
            metric.group,
            metric.measurement_index,
            current.text);
    }
}

void update_metric_reading(const MetricSpec &metric)
{
    uint8_t amount_of_measurements = 0;
    uint8_t measurement_buffer[64] = {};
    MetricReading &reading = s_state->metrics[s_state->ui.selected_metric];
    const MetricReading previous = reading;

    const auto group_status = s_kwp.readGroup(amount_of_measurements, metric.group, measurement_buffer, sizeof(measurement_buffer));
    if (group_status != KLineKWP1281Lib::SUCCESS) {
        clear_metric_reading(reading);
        return;
    }

    if (metric.measurement_index >= amount_of_measurements) {
        clear_metric_reading(reading);
        return;
    }

    reading.updated_at_ms = esp_log_timestamp();
    reading.units[0] = '\0';
    reading.text[0] = '\0';
    switch (KLineKWP1281Lib::getMeasurementType(
        metric.measurement_index,
        amount_of_measurements,
        measurement_buffer,
        sizeof(measurement_buffer))) {
    case KLineKWP1281Lib::VALUE:
        reading.valid = true;
        reading.type = MetricValueType::Numeric;
        reading.numeric_value = static_cast<float>(KLineKWP1281Lib::getMeasurementValue(
            metric.measurement_index,
            amount_of_measurements,
            measurement_buffer,
            sizeof(measurement_buffer)));
        reading.text[0] = '\0';
        KLineKWP1281Lib::getMeasurementUnits(
            metric.measurement_index,
            amount_of_measurements,
            measurement_buffer,
            sizeof(measurement_buffer),
            reading.units,
            sizeof(reading.units));
        break;
    case KLineKWP1281Lib::TEXT:
        reading.valid = true;
        reading.type = MetricValueType::Text;
        reading.units[0] = '\0';
        KLineKWP1281Lib::getMeasurementText(
            metric.measurement_index,
            amount_of_measurements,
            measurement_buffer,
            sizeof(measurement_buffer),
            reading.text,
            sizeof(reading.text));
        break;
    default:
        clear_metric_reading(reading);
        break;
    }

    log_metric_update_if_changed(metric, previous, reading);
}

void set_kline_status(const char *status)
{
    if (s_state == nullptr || status == nullptr) {
        return;
    }
    snprintf(s_state->kline.status, sizeof(s_state->kline.status), "%s", status);
}

void kline_worker_task(void *arg)
{
    (void)arg;
    const MetricSpec &metric = selected_metric_spec(*s_state);

    set_kline_status("probing");
    s_state->kline.connected = false;
    s_state->kline.configured_module = metric.module;
    s_state->kline.active_module = metric.module;
    s_state->kline.baud = 0;

    while (true) {
        for (size_t i = 0; i < sizeof(kBaudCandidates) / sizeof(kBaudCandidates[0]); i++) {
            const unsigned long baud = kBaudCandidates[i];
            set_kline_status("connecting");

            const auto status = s_kwp.attemptConnect(metric.module, baud, false);
            if (status == KLineKWP1281Lib::SUCCESS) {
                const char *part_number = s_kwp.getPartNumber();
                const char *identification = s_kwp.getIdentification();
                s_state->kline.connected = true;
                s_state->kline.active_module = metric.module;
                s_state->kline.baud = baud;
                snprintf(
                    s_state->kline.part_number,
                    sizeof(s_state->kline.part_number),
                    "%s",
                    part_number != nullptr ? part_number : "");
                snprintf(
                    s_state->kline.identification,
                    sizeof(s_state->kline.identification),
                    "%s",
                    identification != nullptr ? identification : "");
                set_kline_status("connected");
                ESP_LOGI(TAG, "Connected to module 0x%02X at %lu", metric.module, baud);

                while (true) {
                    s_kwp.update();
                    update_metric_reading(metric);
                    vTaskDelay(pdMS_TO_TICKS(250));
                }
            }
        }

        s_state->kline.connected = false;
        s_state->kline.baud = 0;
        clear_metric_reading(s_state->metrics[s_state->ui.selected_metric]);
        set_kline_status("retry");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

} // namespace

esp_err_t kline_service_init(AppState &state)
{
    ESP_RETURN_ON_ERROR(kline_uart_init(kDefaultKlineConfig), "kline_service", "uart init failed");
    s_state = &state;
    state.kline.transport_ready = true;
    state.kline.connected = false;
    state.kline.configured_module = kDefaultModule;
    state.kline.active_module = 0;
    state.kline.baud = 0;
    set_kline_status("ready");

    BaseType_t rc = xTaskCreate(kline_worker_task, "kline_worker", 8192, nullptr, 5, nullptr);
    if (rc != pdPASS) {
        return ESP_FAIL;
    }
    return ESP_OK;
}
