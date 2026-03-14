#pragma once
#include "data_structs.h"
#include <vector>

class forecast_engine {
public:
    forecast_engine() = default;

    // Compute ensemble from NWP model forecasts.
    // Returns forecasts sorted by date ascending.
    std::vector<ensemble_forecast> compute(const std::vector<nwp_model_forecast>& models);
};
