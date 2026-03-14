#include "config.h"
#include "data_aggregator.h"
#include "tui_renderer.h"

#include <curl/curl.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <memory>

int main() {
    try {
        auto logger = spdlog::basic_logger_mt(
            "file_logger", "toronto_weather_dash.log", /*truncate=*/true);
        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::info);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
        spdlog::flush_on(spdlog::level::info);
    }
    catch (...) {}

    curl_global_init(CURL_GLOBAL_DEFAULT);

    auto cfg      = std::make_shared<config>("config.yaml");
    auto aggr     = std::make_shared<data_aggregator>(cfg);
    tui_renderer renderer(aggr);

    aggr->start();
    renderer.run();
    aggr->stop();

    curl_global_cleanup();
    return 0;
}
