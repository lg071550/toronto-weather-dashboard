#include "open_meteo_hourly_fetcher.h"
#include "utils.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <string>

std::vector<hourly_obs> open_meteo_hourly_fetcher::fetch(http_client& http,
                                                          double lat, double lon) {
    const std::string url =
        std::string("https://api.open-meteo.com/v1/forecast?")
        + "latitude="  + utils::format_float(static_cast<float>(lat), 4)
        + "&longitude=" + utils::format_float(static_cast<float>(lon), 4)
        + "&hourly=temperature_2m,precipitation,windspeed_10m,cloudcover"
        + "&timezone=America%2FToronto"
        + "&forecast_days=3";

    spdlog::debug("open_meteo_hourly_fetcher GET {}", url);
    const std::string body = http.get(url);
    const auto json = nlohmann::json::parse(body);

    std::vector<hourly_obs> results;

    if (!json.contains("hourly")) {
        spdlog::warn("open_meteo_hourly_fetcher: no hourly object in response");
        return results;
    }

    const auto& hourly = json["hourly"];
    const auto& times  = hourly["time"];
    const auto& temps  = hourly["temperature_2m"];

    const auto& precip = hourly.contains("precipitation")
                             ? hourly["precipitation"]
                             : nlohmann::json::array();

    nlohmann::json wind_arr = nlohmann::json::array();
    if (hourly.contains("windspeed_10m"))       wind_arr = hourly["windspeed_10m"];
    else if (hourly.contains("wind_speed_10m")) wind_arr = hourly["wind_speed_10m"];

    nlohmann::json cloud_arr = nlohmann::json::array();
    if (hourly.contains("cloudcover"))          cloud_arr = hourly["cloudcover"];
    else if (hourly.contains("cloud_cover"))    cloud_arr = hourly["cloud_cover"];

    for (size_t i = 0; i < times.size(); ++i) {
        try {
            hourly_obs obs;

            // Store full "YYYY-MM-DD HH:MM" so the TUI can show day context
            const std::string raw = times[i].get<std::string>(); // "YYYY-MM-DDTHH:MM"
            if (raw.size() >= 16) {
                obs.hour_label = raw.substr(0, 10) + ' ' + raw.substr(11, 5);
            } else {
                obs.hour_label = raw;
            }

            obs.temp_c    = (i < temps.size()     && !temps[i].is_null())     ? temps[i].get<float>()     : 0.0f;
            obs.precip_mm = (i < precip.size()    && !precip[i].is_null())    ? precip[i].get<float>()    : 0.0f;
            obs.wind_kmh  = (i < wind_arr.size()  && !wind_arr[i].is_null())  ? wind_arr[i].get<float>()  : 0.0f;
            obs.cloud_pct = (i < cloud_arr.size() && !cloud_arr[i].is_null()) ? cloud_arr[i].get<int>()   : 0;

            results.push_back(std::move(obs));
        }
        catch (const std::exception& e) {
            spdlog::warn("open_meteo_hourly_fetcher: failed to parse hour {}: {}", i, e.what());
        }
    }

    spdlog::info("open_meteo_hourly_fetcher: parsed {} hourly entries", results.size());
    return results;
}
