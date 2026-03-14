#include "data_aggregator.h"
#include "utils.h"
#include <spdlog/spdlog.h>
#include <algorithm>

// ---------------------------------------------------------------------------
// Static helper
// ---------------------------------------------------------------------------
int data_aggregator::annotate_ensemble_changes(
    const std::vector<ensemble_forecast>& previous,
    std::vector<ensemble_forecast>& current) {

    int changed_days = 0;
    for (auto& cur : current) {
        auto it = std::find_if(previous.begin(), previous.end(),
                               [&](const ensemble_forecast& p) { return p.date == cur.date; });
        if (it == previous.end()) continue;

        cur.has_previous           = true;
        cur.delta_from_previous    = cur.weighted_tmax - it->weighted_tmax;
        cur.changed_since_previous = std::abs(cur.delta_from_previous) >= 0.1f;
        if (cur.changed_since_previous) ++changed_days;
    }
    return changed_days;
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------
data_aggregator::data_aggregator(std::shared_ptr<config> cfg)
    : config_(std::move(cfg)) {
    const auto& stations = config_->metar_stations();
    primary_station_id_  = stations.empty() ? "CYYZ" : stations.front();
}

data_aggregator::~data_aggregator() {
    stop();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void data_aggregator::start() {
    if (running_.exchange(true)) return;
    threads_.emplace_back(&data_aggregator::metar_loop,  this);
    threads_.emplace_back(&data_aggregator::nwp_loop,    this);
    threads_.emplace_back(&data_aggregator::hourly_loop, this);
    spdlog::info("data_aggregator: started 3 fetcher threads");
}

void data_aggregator::stop() {
    if (!running_.exchange(false)) return;
    cv_.notify_all();
    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
    threads_.clear();
    spdlog::info("data_aggregator: all threads joined");
}

void data_aggregator::force_refresh() {
    ++refresh_gen_;
    cv_.notify_all();
}

void data_aggregator::set_on_change(std::function<void()> cb) {
    on_change_ = std::move(cb);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------
void data_aggregator::sleep_for(int seconds) {
    std::unique_lock<std::mutex> lock(cv_mutex_);
    const int gen = refresh_gen_.load();
    cv_.wait_for(lock, std::chrono::seconds(seconds), [this, gen] {
        return !running_.load() || refresh_gen_.load() != gen;
    });
}

void data_aggregator::recompute_ensemble_locked() {
    // Must be called while holding a unique_lock on data_mutex_
    auto next = forecast_engine_.compute(nwp_forecasts_);
    const int changed = annotate_ensemble_changes(ensemble_forecasts_, next);
    ensemble_forecasts_ = std::move(next);
    nwp_change_summary_ = changed > 0
        ? ("\xCE\x94 " + std::to_string(changed) + (changed == 1 ? " day" : " days"))
        : "steady";
}

// ---------------------------------------------------------------------------
// Fetch loops
// ---------------------------------------------------------------------------
void data_aggregator::metar_loop() {
    http_client http;
    while (running_) {
        try {
            auto reports = metar_fetcher_.fetch(http, config_->metar_stations());
            auto tafs    = taf_fetcher_.fetch(http, config_->metar_stations());
            {
                std::unique_lock lock(data_mutex_);
                metar_reports_ = std::move(reports);
                taf_forecasts_ = std::move(tafs);

                auto it = std::find_if(metar_reports_.begin(), metar_reports_.end(),
                                       [&](const metar_report& r) {
                                           return r.station_id == primary_station_id_;
                                       });

                if (it != metar_reports_.end()) {
                    primary_metar_     = *it;
                    has_primary_metar_ = true;

                    const std::string obs_date = primary_metar_.obs_time.size() >= 10
                        ? primary_metar_.obs_time.substr(0, 10)
                        : utils::current_utc_date_string();

                    if (!has_primary_station_day_high_ ||
                        primary_station_day_high_date_ != obs_date) {
                        primary_station_day_high_date_  = obs_date;
                        primary_station_day_high_c_     = primary_metar_.temp_c;
                        has_primary_station_day_high_   = true;
                    } else {
                        primary_station_day_high_c_ =
                            std::max(primary_station_day_high_c_, primary_metar_.temp_c);
                    }
                } else {
                    has_primary_metar_ = false;
                }

                metar_error_.clear();
                taf_error_.clear();
                metar_last_refresh_ = utils::current_utc_time_string();
                taf_last_refresh_   = metar_last_refresh_;
            }
        }
        catch (const std::exception& e) {
            spdlog::warn("metar_loop: {}", e.what());
            std::unique_lock lock(data_mutex_);
            metar_error_ = e.what();
            taf_error_   = e.what();
        }

        if (on_change_) on_change_();
        sleep_for(config_->metar_refresh_seconds());
    }
}

void data_aggregator::nwp_loop() {
    http_client http;
    while (running_) {
        try {
            auto forecasts = nwp_fetcher_.fetch(http,
                                                config_->toronto_lat(),
                                                config_->toronto_lon());
            {
                std::unique_lock lock(data_mutex_);
                nwp_forecasts_ = std::move(forecasts);
                recompute_ensemble_locked();
                nwp_error_.clear();
                nwp_last_refresh_ = utils::current_utc_time_string();
            }
        }
        catch (const std::exception& e) {
            spdlog::warn("nwp_loop: {}", e.what());
            std::unique_lock lock(data_mutex_);
            nwp_error_ = e.what();
        }

        if (on_change_) on_change_();
        sleep_for(config_->nwp_refresh_seconds());
    }
}

void data_aggregator::hourly_loop() {
    http_client http;
    while (running_) {
        try {
            auto obs = hourly_fetcher_.fetch(http,
                                             config_->toronto_lat(),
                                             config_->toronto_lon());
            {
                std::unique_lock lock(data_mutex_);
                hourly_obs_ = std::move(obs);
                hourly_error_.clear();
                hourly_last_refresh_ = utils::current_utc_time_string();
            }
        }
        catch (const std::exception& e) {
            spdlog::warn("hourly_loop: {}", e.what());
            std::unique_lock lock(data_mutex_);
            hourly_error_ = e.what();
        }

        if (on_change_) on_change_();
        sleep_for(config_->hourly_refresh_seconds());
    }
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------
bool data_aggregator::has_primary_metar() const {
    std::shared_lock lock(data_mutex_);
    return has_primary_metar_;
}
metar_report data_aggregator::get_primary_metar() const {
    std::shared_lock lock(data_mutex_);
    return primary_metar_;
}
bool data_aggregator::has_primary_station_day_high() const {
    std::shared_lock lock(data_mutex_);
    return has_primary_station_day_high_;
}
float data_aggregator::get_primary_station_day_high() const {
    std::shared_lock lock(data_mutex_);
    return primary_station_day_high_c_;
}
std::string data_aggregator::get_primary_station_id() const {
    std::shared_lock lock(data_mutex_);
    return primary_station_id_;
}
std::vector<metar_report> data_aggregator::get_metar_reports() const {
    std::shared_lock lock(data_mutex_);
    return metar_reports_;
}
std::vector<taf_forecast> data_aggregator::get_taf_forecasts() const {
    std::shared_lock lock(data_mutex_);
    return taf_forecasts_;
}
std::vector<nwp_model_forecast> data_aggregator::get_nwp_forecasts() const {
    std::shared_lock lock(data_mutex_);
    return nwp_forecasts_;
}
std::vector<hourly_obs> data_aggregator::get_hourly_obs() const {
    std::shared_lock lock(data_mutex_);
    return hourly_obs_;
}
std::vector<ensemble_forecast> data_aggregator::get_ensemble_forecasts() const {
    std::shared_lock lock(data_mutex_);
    return ensemble_forecasts_;
}
std::string data_aggregator::get_metar_error() const {
    std::shared_lock lock(data_mutex_);
    return metar_error_;
}
std::string data_aggregator::get_taf_error() const {
    std::shared_lock lock(data_mutex_);
    return taf_error_;
}
std::string data_aggregator::get_nwp_error() const {
    std::shared_lock lock(data_mutex_);
    return nwp_error_;
}
std::string data_aggregator::get_hourly_error() const {
    std::shared_lock lock(data_mutex_);
    return hourly_error_;
}
std::string data_aggregator::get_nwp_change_summary() const {
    std::shared_lock lock(data_mutex_);
    return nwp_change_summary_;
}
std::string data_aggregator::get_metar_last_refresh() const {
    std::shared_lock lock(data_mutex_);
    return metar_last_refresh_;
}
std::string data_aggregator::get_taf_last_refresh() const {
    std::shared_lock lock(data_mutex_);
    return taf_last_refresh_;
}
std::string data_aggregator::get_nwp_last_refresh() const {
    std::shared_lock lock(data_mutex_);
    return nwp_last_refresh_;
}
std::string data_aggregator::get_hourly_last_refresh() const {
    std::shared_lock lock(data_mutex_);
    return hourly_last_refresh_;
}
