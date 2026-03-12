#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"

#include "ble_service.hpp"
#include "device_service.hpp"
#include "display_service.hpp"
#include "kline_service.hpp"
#include "state.hpp"
#include "ui_service.hpp"

static const char *TAG = "app";

extern "C" void app_main(void)
{
    AppState &state = app_state();

    esp_log_level_set("KWP", ESP_LOG_NONE);
    ESP_LOGI(TAG, "Booting application...");
    ESP_ERROR_CHECK(ble_service_init());
    ESP_ERROR_CHECK(device_service_init(state));
    ESP_ERROR_CHECK(display_service_init());
    ESP_ERROR_CHECK(ui_service_init(state));
    ESP_ERROR_CHECK(kline_service_init(state));

    while (1) {
        state.ui.frame_counter++;
        device_service_poll(state);
        ui_service_refresh(state);
        display_service_task_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
