#pragma once

#include "esp_err.h"

struct AppState;

esp_err_t device_service_init(AppState &state);
void device_service_poll(AppState &state);
