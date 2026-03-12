#pragma once

#include <stddef.h>
#include <stdint.h>

enum class MetricLayout : uint8_t {
    Temperature,
    Speed,
    Rpm,
    Voltage,
    Numeric,
    Text,
};

enum class MetricValueType : uint8_t {
    Empty,
    Numeric,
    Text,
};

enum class MetricFormat : uint8_t {
    TemperatureC,
    SpeedKmh,
    Rpm,
    Voltage,
    Generic,
    Text,
};

enum class MetricComponentType : uint8_t {
    NumericWithSuffix,
    TextOnly,
};

struct MetricSpec {
    const char *id;
    const char *title;
    MetricLayout layout;
    MetricFormat format;
    MetricComponentType component;
    uint8_t decimals;
    const char *suffix;
    uint8_t module;
    uint8_t group;
    uint8_t measurement_index;
};

struct MetricReading {
    bool valid = false;
    MetricValueType type = MetricValueType::Empty;
    uint32_t updated_at_ms = 0;
    float numeric_value = 0.0f;
    char units[16] = "";
    char text[32] = "";
};

struct UiState {
    bool ready = false;
    size_t selected_metric = 0;
    uint32_t frame_counter = 0;
};

struct DeviceState {
    bool cpu_temp_valid = false;
    float cpu_temp_c = 0.0f;
};

struct KlineSessionState {
    bool transport_ready = false;
    bool connected = false;
    uint8_t configured_module = 0;
    uint8_t active_module = 0;
    uint32_t baud = 0;
    char status[32] = "idle";
    char part_number[16] = "";
    char identification[32] = "";
};

constexpr size_t kMetricCapacity = 8;

struct AppState {
    UiState ui;
    DeviceState device;
    KlineSessionState kline;
    MetricReading metrics[kMetricCapacity];
};

AppState &app_state();
