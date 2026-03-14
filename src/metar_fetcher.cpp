#include "metar_fetcher.h"
#include "utils.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <sstream>

std::vector<metar_report> metar_fetcher::fetch(http_client& http,
                                                const std::vector<std::string>& stations) {
    std::string ids;
    for (size_t i = 0; i < stations.size(); ++i) {
        if (i > 0) ids += ',';
        ids += stations[i];
    }

    const std::string url = "https://aviationweather.gov/api/data/metar?ids=" + ids + "&format=json";
    spdlog::debug("metar_fetcher GET {}", url);

    const std::string body = http.get(url);
    const auto json = nlohmann::json::parse(body);

    std::vector<metar_report> reports;

    if (!json.is_array()) {
        spdlog::warn("metar_fetcher: response is not a JSON array");
        return reports;
    }

    for (const auto& m : json) {
        try {
            metar_report r;
            r.station_id  = m.value("icaoId", "");
            r.raw_text    = m.value("rawOb", "");
            r.obs_time    = m.value("reportTime", "");
            r.is_speci    = m.value("metarType", "") == "SPECI" || r.raw_text.rfind("SPECI", 0) == 0;
            r.temp_c      = m.value("temp", 0.0f);
            r.dewpoint_c  = m.value("dewp", 0.0f);

            if (m.contains("wdir")) {
                r.wind_dir_deg = m["wdir"].is_string()
                    ? m["wdir"].get<std::string>()
                    : std::to_string(m["wdir"].get<int>());
            }

            r.wind_speed_kt = m.value("wspd", 0);

            if (m.contains("visib")) {
                r.visibility = m["visib"].is_string()
                    ? m["visib"].get<std::string>()
                    : utils::format_float(m["visib"].get<float>(), 0);
            }

            if (m.contains("clouds") && m["clouds"].is_array()) {
                std::string sky;
                for (const auto& cloud : m["clouds"]) {
                    if (!sky.empty()) sky += ' ';
                    sky += cloud.value("cover", "");
                    if (cloud.contains("base") && !cloud["base"].is_null()) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "%03d", cloud["base"].get<int>());
                        sky += buf;
                    }
                }
                r.sky_condition = sky;
            }

            if (m.contains("wxString") && !m["wxString"].is_null()) {
                r.wx_string = m["wxString"].get<std::string>();
            }

            r.age_minutes = utils::compute_age_minutes(r.obs_time);
            reports.push_back(std::move(r));
        }
        catch (const std::exception& e) {
            spdlog::warn("metar_fetcher: failed to parse entry: {}", e.what());
        }
    }

    spdlog::info("metar_fetcher: parsed {} reports", reports.size());
    return reports;
}
