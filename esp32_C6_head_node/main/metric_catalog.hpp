#pragma once

#include <stddef.h>

#include "state.hpp"

const MetricSpec *metric_catalog();
size_t metric_catalog_size();
const MetricSpec &selected_metric_spec(const AppState &state);
