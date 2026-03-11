#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t lvgl_port_init(void);
void lvgl_port_task_handler(void);
void lvgl_port_create_center_test_ui(const char *text);

#ifdef __cplusplus
}
#endif
