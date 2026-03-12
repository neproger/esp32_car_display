#pragma once

#include "esp_err.h"

struct AppState;

esp_err_t ui_service_init(AppState &state);
void ui_service_refresh(AppState &state);
