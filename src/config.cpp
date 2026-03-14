#include "config.h"
#include <yaml-cpp/yaml.h>
#include <spdlog/spdlog.h>
#include <filesystem>

config::config(const std::string& path) {
    if (!std::filesystem::exists(path)) {
        spdlog::warn("Config file '{}' not found, using defaults", path);
        metar_stations_ = {"CYYZ", "CYZD", "CYKZ", "CYOO"};
        return;
    }

    try {
        YAML::Node root = YAML::LoadFile(path);

        if (root["metar_stations"]) {
            for (const auto& node : root["metar_stations"]) {
                metar_stations_.push_back(node.as<std::string>());
            }
        }
        if (metar_stations_.empty()) {
            metar_stations_ = {"CYYZ", "CYZD", "CYKZ", "CYOO"};
        }

        if (root["refresh_intervals_seconds"]) {
            const auto& ri = root["refresh_intervals_seconds"];
            if (ri["nwp"])    nwp_refresh_    = ri["nwp"].as<int>(300);
            if (ri["metar"])  metar_refresh_  = ri["metar"].as<int>(60);
            if (ri["hourly"]) hourly_refresh_ = ri["hourly"].as<int>(300);
        }

        if (root["toronto"]) {
            const auto& tor = root["toronto"];
            if (tor["lat"]) lat_ = tor["lat"].as<double>(43.6777);
            if (tor["lon"]) lon_ = tor["lon"].as<double>(-79.6248);
        }

        spdlog::info("Config loaded from '{}'", path);
    }
    catch (const std::exception& e) {
        spdlog::error("Failed to parse config '{}': {}", path, e.what());
        metar_stations_ = {"CYYZ", "CYZD", "CYKZ", "CYOO"};
    }
}
