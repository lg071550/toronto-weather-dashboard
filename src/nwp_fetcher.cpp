#include "nwp_fetcher.h"
#include "utils.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <string>

namespace {
    struct model_def {
        const char* label;
        const char* slug;
    };

    constexpr model_def k_models[] = {
        {"ecmwf", "ecmwf_ifs025"},
        {"gfs",   "gfs_seamless"},
        {"icon",  "icon_seamless"},
        {"gem",   "gem_seamless"},   // Environment Canada / ECCC
    };
} // namespace

std::vector<nwp_model_forecast> nwp_fetcher::fetch(http_client& http,
                                                    double lat, double lon) {
    std::vector<nwp_model_forecast> results;

    for (const auto& model : k_models) {
        try {
            const std::string url =
                std::string("https://api.open-meteo.com/v1/forecast?")
                + "latitude="  + utils::format_float(static_cast<float>(lat), 4)
                + "&longitude=" + utils::format_float(static_cast<float>(lon), 4)
                + "&daily=temperature_2m_max,temperature_2m_min"
                + "&timezone=America%2FToronto"
                + "&forecast_days=5"
                + "&models=" + model.slug;

            spdlog::debug("nwp_fetcher GET {} ({})", url, model.label);
            const std::string body = http.get(url);
            const auto json = nlohmann::json::parse(body);

            nwp_model_forecast forecast;
            forecast.model    = model.label;
            forecast.run_time = utils::current_utc_time_string();

            if (json.contains("daily")) {
                const auto& daily = json["daily"];
                const auto& times = daily["time"];
                const auto& tmax  = daily["temperature_2m_max"];
                const auto& tmin  = daily["temperature_2m_min"];

                for (size_t i = 0; i < times.size(); ++i) {
                    const std::string date = times[i].get<std::string>();
                    if (i < tmax.size() && !tmax[i].is_null())
                        forecast.tmax_by_date[date] = tmax[i].get<float>();
                    if (i < tmin.size() && !tmin[i].is_null())
                        forecast.tmin_by_date[date] = tmin[i].get<float>();
                }
            }

            forecast.age_minutes = 0;
            if (!forecast.tmax_by_date.empty()) {
                spdlog::info("nwp_fetcher: {} returned {} days",
                             model.label, forecast.tmax_by_date.size());
                results.push_back(std::move(forecast));
            } else {
                spdlog::warn("nwp_fetcher: {} returned no data", model.label);
            }
        }
        catch (const std::exception& e) {
            spdlog::warn("nwp_fetcher: {} fetch failed: {}", model.label, e.what());
        }
    }

    return results;
}
