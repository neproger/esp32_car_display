#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ble_central_service.hpp"

static const char *TAG = "app";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Booting ESP32-C6 controller...");
    ESP_ERROR_CHECK(ble_central_service_init());

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
