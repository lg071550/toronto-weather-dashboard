#include "forecast_engine.h"
#include "utils.h"
#include <set>
#include <cmath>
#include <algorithm>
#include <numeric>

std::vector<ensemble_forecast> forecast_engine::compute(
    const std::vector<nwp_model_forecast>& models) {

    std::set<std::string> all_dates;
    for (const auto& m : models) {
        for (const auto& [date, _] : m.tmax_by_date) {
            all_dates.insert(date);
        }
    }

    std::vector<ensemble_forecast> results;
    results.reserve(all_dates.size());

    for (const auto& date : all_dates) {
        ensemble_forecast ens;
        ens.date = date;

        std::vector<float> values;
        for (const auto& m : models) {
            auto it = m.tmax_by_date.find(date);
            if (it != m.tmax_by_date.end()) {
                values.push_back(it->second);
                ens.model_tmax[m.model] = it->second;
            }
        }

        ens.model_count = static_cast<int>(values.size());
        if (ens.model_count == 0) continue;

        const float sum = std::accumulate(values.begin(), values.end(), 0.0f);
        ens.weighted_tmax = sum / static_cast<float>(ens.model_count);

        ens.spread = *std::max_element(values.begin(), values.end())
                   - *std::min_element(values.begin(), values.end());

        if (ens.model_count > 1) {
            const float mean = ens.weighted_tmax;
            float sq_sum = 0.0f;
            for (float v : values) {
                const float d = v - mean;
                sq_sum += d * d;
            }
            ens.sigma = std::sqrt(sq_sum / static_cast<float>(ens.model_count)) * 2.5f;
        } else {
            ens.sigma = 0.0f;
        }

        ens.models_agree = (ens.spread < 1.5f);
        results.push_back(std::move(ens));
    }

    std::sort(results.begin(), results.end(),
              [](const ensemble_forecast& a, const ensemble_forecast& b) {
                  return a.date < b.date;
              });

    return results;
}
