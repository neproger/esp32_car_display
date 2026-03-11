#include "metric_catalog.hpp"

namespace {

constexpr MetricSpec kMetricCatalog[] = {
    {"engine_temp", "TEMP", MetricLayout::Temperature, 0x01, 0x04, 0},
};

} // namespace

const MetricSpec *metric_catalog()
{
    return kMetricCatalog;
}

size_t metric_catalog_size()
{
    return sizeof(kMetricCatalog) / sizeof(kMetricCatalog[0]);
}

const MetricSpec &selected_metric_spec(const AppState &state)
{
    const size_t count = metric_catalog_size();
    const size_t index = (count == 0) ? 0 : (state.ui.selected_metric % count);
    return kMetricCatalog[index];
}
