#include "taf_fetcher.h"
#include "utils.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <sstream>
#include <ctime>

namespace {
    std::string join_stations(const std::vector<std::string>& stations) {
        std::string ids;
        for (size_t i = 0; i < stations.size(); ++i) {
            if (i > 0) ids += ',';
            ids += stations[i];
        }
        return ids;
    }

    std::string format_zulu_time(long long unix_seconds) {
        std::time_t t = static_cast<std::time_t>(unix_seconds);
        std::tm gm{};
#ifdef _WIN32
        gmtime_s(&gm, &t);
#else
        gmtime_r(&t, &gm);
#endif
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%02d%02dZ", gm.tm_hour, gm.tm_min);
        return std::string(buf);
    }

    std::string visibility_string(const nlohmann::json& value) {
        if (value.is_string()) return value.get<std::string>();
        if (value.is_number()) return utils::format_float(value.get<float>(), 1) + "SM";
        return {};
    }

    int lowest_ceiling_feet(const nlohmann::json& clouds) {
        if (!clouds.is_array()) return -1;
        int lowest = -1;
        for (const auto& cloud : clouds) {
            const std::string cover = cloud.value("cover", "");
            if ((cover == "BKN" || cover == "OVC") &&
                cloud.contains("base") && !cloud["base"].is_null()) {
                const int base = cloud["base"].get<int>();
                if (lowest < 0 || base < lowest) lowest = base;
            }
        }
        return lowest;
    }
} // namespace

std::vector<taf_forecast> taf_fetcher::fetch(http_client& http,
                                              const std::vector<std::string>& stations) {
    std::vector<taf_forecast> forecasts;
    const std::string url = "https://aviationweather.gov/api/data/taf?ids=" +
                            join_stations(stations) + "&format=json";
    spdlog::debug("taf_fetcher GET {}", url);

    const std::string body = http.get(url);
    const auto json = nlohmann::json::parse(body);

    if (!json.is_array()) {
        spdlog::warn("taf_fetcher: response is not a JSON array");
        return forecasts;
    }

    for (const auto& taf : json) {
        try {
            taf_forecast forecast;
            forecast.station_id = taf.value("icaoId", "");
            if (forecast.station_id.empty()) forecast.station_id = taf.value("name", "");
            forecast.issue_time  = taf.value("issueTime", "");
            forecast.raw_text    = taf.value("rawTAF", "");
            forecast.age_minutes = utils::compute_age_minutes(forecast.issue_time);

            if (taf.contains("fcsts") && taf["fcsts"].is_array()) {
                for (const auto& fcst : taf["fcsts"]) {
                    std::vector<std::string> hazards;

                    const int ceiling = lowest_ceiling_feet(
                        fcst.value("clouds", nlohmann::json::array()));
                    if (ceiling > 0 && ceiling <= 1500) {
                        hazards.push_back("ceiling " + std::to_string(ceiling) + "ft");
                    }

                    if (fcst.contains("visib") && !fcst["visib"].is_null()) {
                        const std::string vis = visibility_string(fcst["visib"]);
                        const bool low_vis = !fcst["visib"].is_string() &&
                                            fcst["visib"].get<float>() < 5.0f;
                        if (!vis.empty() && (low_vis || vis != "6+")) {
                            hazards.push_back("vis " + vis);
                        }
                    }

                    const int gust = fcst.value("wgst", 0);
                    if (gust >= 25) hazards.push_back("gust " + std::to_string(gust) + "kt");

                    const std::string wx = fcst.value("wxString", "");
                    if (!wx.empty()) hazards.push_back(wx);

                    if (hazards.empty()) continue;

                    std::string label;
                    if (fcst.contains("probability") && !fcst["probability"].is_null()) {
                        label = "PROB" + std::to_string(fcst["probability"].get<int>());
                    }
                    const std::string change = fcst.value("fcstChange", "");
                    if (!change.empty()) {
                        if (!label.empty()) label += ' ';
                        label += change;
                    }
                    if (label.empty()) label = "BASE";

                    std::ostringstream alert;
                    alert << label << ' '
                          << format_zulu_time(fcst.value("timeFrom", 0LL))
                          << '-'
                          << format_zulu_time(fcst.value("timeTo", 0LL))
                          << ' ';
                    for (size_t i = 0; i < hazards.size(); ++i) {
                        if (i > 0) alert << ", ";
                        alert << hazards[i];
                    }
                    forecast.alerts.push_back(alert.str());
                }
            }

            forecasts.push_back(std::move(forecast));
        }
        catch (const std::exception& e) {
            spdlog::warn("taf_fetcher: failed to parse entry: {}", e.what());
        }
    }

    spdlog::info("taf_fetcher: parsed {} forecasts", forecasts.size());
    return forecasts;
}
