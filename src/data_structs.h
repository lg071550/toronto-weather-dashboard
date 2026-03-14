#pragma once
#include <string>
#include <map>
#include <vector>

struct metar_report {
    std::string station_id;
    std::string raw_text;
    std::string obs_time;
    float temp_c        = 0.0f;
    float dewpoint_c    = 0.0f;
    std::string wind_dir_deg;
    int wind_speed_kt   = 0;
    std::string visibility;
    std::string sky_condition;
    std::string wx_string;
    bool is_speci       = false;
    int age_minutes     = -1;
};

struct taf_forecast {
    std::string station_id;
    std::string issue_time;
    std::string raw_text;
    std::vector<std::string> alerts;
    int age_minutes = -1;
};

struct nwp_model_forecast {
    std::string model;
    std::string run_time;
    std::map<std::string, float> tmax_by_date;
    std::map<std::string, float> tmin_by_date;
    int age_minutes = -1;
};

struct hourly_obs {
    std::string hour_label; // "YYYY-MM-DD HH:MM" local-ish (America/Toronto)
    float temp_c    = 0.0f;
    float precip_mm = 0.0f;
    float wind_kmh  = 0.0f;
    int cloud_pct   = 0;
};

struct ensemble_forecast {
    std::string date;
    float weighted_tmax         = 0.0f;
    float spread                = 0.0f;
    float sigma                 = 0.0f;
    bool models_agree           = false;
    float delta_from_previous   = 0.0f;
    bool has_previous           = false;
    bool changed_since_previous = false;
    std::map<std::string, float> model_tmax;
    int model_count             = 0;
};
