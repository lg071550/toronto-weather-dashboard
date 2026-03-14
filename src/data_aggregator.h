#pragma once
#include "config.h"
#include "http_client.h"
#include "metar_fetcher.h"
#include "taf_fetcher.h"
#include "nwp_fetcher.h"
#include "open_meteo_hourly_fetcher.h"
#include "forecast_engine.h"
#include "data_structs.h"

#include <memory>
#include <vector>
#include <string>
#include <thread>
#include <shared_mutex>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>

class data_aggregator {
public:
    explicit data_aggregator(std::shared_ptr<config> cfg);
    ~data_aggregator();

    data_aggregator(const data_aggregator&) = delete;
    data_aggregator& operator=(const data_aggregator&) = delete;

    void start();
    void stop();
    void force_refresh();
    void set_on_change(std::function<void()> cb);

    // Primary station accessors
    bool                  has_primary_metar()          const;
    metar_report          get_primary_metar()           const;
    bool                  has_primary_station_day_high() const;
    float                 get_primary_station_day_high() const;
    std::string           get_primary_station_id()      const;

    // Bulk accessors
    std::vector<metar_report>       get_metar_reports()     const;
    std::vector<taf_forecast>       get_taf_forecasts()     const;
    std::vector<nwp_model_forecast> get_nwp_forecasts()     const;
    std::vector<hourly_obs>         get_hourly_obs()        const;
    std::vector<ensemble_forecast>  get_ensemble_forecasts() const;

    // Error strings
    std::string get_metar_error()   const;
    std::string get_taf_error()     const;
    std::string get_nwp_error()     const;
    std::string get_hourly_error()  const;
    std::string get_nwp_change_summary() const;

    // Last-refresh timestamps
    std::string get_metar_last_refresh()   const;
    std::string get_taf_last_refresh()     const;
    std::string get_nwp_last_refresh()     const;
    std::string get_hourly_last_refresh()  const;

private:
    std::shared_ptr<config> config_;

    // Fetchers (each loop owns its own http_client to avoid contention)
    metar_fetcher              metar_fetcher_;
    taf_fetcher                taf_fetcher_;
    nwp_fetcher                nwp_fetcher_;
    open_meteo_hourly_fetcher  hourly_fetcher_;
    forecast_engine            forecast_engine_;

    // Shared state — protected by data_mutex_
    mutable std::shared_mutex data_mutex_;
    std::string               primary_station_id_;
    metar_report              primary_metar_;
    bool                      has_primary_metar_           = false;
    float                     primary_station_day_high_c_  = 0.0f;
    std::string               primary_station_day_high_date_;
    bool                      has_primary_station_day_high_ = false;

    std::vector<metar_report>       metar_reports_;
    std::vector<taf_forecast>       taf_forecasts_;
    std::vector<nwp_model_forecast> nwp_forecasts_;
    std::vector<hourly_obs>         hourly_obs_;
    std::vector<ensemble_forecast>  ensemble_forecasts_;

    std::string metar_error_;
    std::string taf_error_;
    std::string nwp_error_;
    std::string hourly_error_;
    std::string nwp_change_summary_;

    std::string metar_last_refresh_;
    std::string taf_last_refresh_;
    std::string nwp_last_refresh_;
    std::string hourly_last_refresh_;

    // Threading
    std::atomic<bool> running_{false};
    std::atomic<int>  refresh_gen_{0};
    std::mutex        cv_mutex_;
    std::condition_variable cv_;
    std::vector<std::thread> threads_;
    std::function<void()> on_change_;

    // Loops
    void metar_loop();
    void nwp_loop();
    void hourly_loop();

    // Helpers — must be called with data_mutex_ held for writing
    void recompute_ensemble_locked();
    void sleep_for(int seconds);

    // Annotate deltas from previous ensemble — returns number of changed days
    static int annotate_ensemble_changes(const std::vector<ensemble_forecast>& previous,
                                         std::vector<ensemble_forecast>& current);
};
