#include "device_service.hpp"

#include "driver/temperature_sensor.h"
#include "esp_check.h"
#include "esp_log.h"

#include "state.hpp"

namespace {

constexpr uint32_t kCpuTempPollIntervalMs = 1000;

const char *TAG = "device";
temperature_sensor_handle_t s_temp_sensor = nullptr;
uint32_t s_last_poll_ms = 0;

} // namespace

esp_err_t device_service_init(AppState &state)
{
    temperature_sensor_config_t temp_sensor_config = {};
    temp_sensor_config.range_min = 10;
    temp_sensor_config.range_max = 80;
    temp_sensor_config.clk_src = TEMPERATURE_SENSOR_CLK_SRC_DEFAULT;
    temp_sensor_config.flags.allow_pd = 0;
    ESP_RETURN_ON_ERROR(temperature_sensor_install(&temp_sensor_config, &s_temp_sensor), TAG, "temperature_sensor_install failed");
    ESP_RETURN_ON_ERROR(temperature_sensor_enable(s_temp_sensor), TAG, "temperature_sensor_enable failed");

    state.device.cpu_temp_valid = false;
    state.device.cpu_temp_c = 0.0f;
    s_last_poll_ms = 0;

    return ESP_OK;
}

void device_service_poll(AppState &state)
{
    if (s_temp_sensor == nullptr) {
        state.device.cpu_temp_valid = false;
        return;
    }

    const uint32_t now_ms = state.ui.frame_counter * 10U;
    if ((now_ms - s_last_poll_ms) < kCpuTempPollIntervalMs) {
        return;
    }
    s_last_poll_ms = now_ms;

    float celsius = 0.0f;
    if (temperature_sensor_get_celsius(s_temp_sensor, &celsius) == ESP_OK) {
        state.device.cpu_temp_valid = true;
        state.device.cpu_temp_c = celsius;
    } else {
        state.device.cpu_temp_valid = false;
    }
}
