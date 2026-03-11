#include "display_service.hpp"

#include "esp_check.h"

#include "lvgl_port.hpp"
#include "waveshare_147.hpp"

esp_err_t display_service_init()
{
    ESP_RETURN_ON_ERROR(waveshare_147_init(), "display", "lcd init failed");
    ESP_RETURN_ON_ERROR(lvgl_port_init(), "display", "lvgl init failed");
    return ESP_OK;
}

void display_service_task_handler()
{
    lvgl_port_task_handler();
}
