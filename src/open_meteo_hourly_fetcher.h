#pragma once
#include "data_structs.h"
#include "http_client.h"
#include <vector>

class open_meteo_hourly_fetcher {
public:
    open_meteo_hourly_fetcher() = default;
    std::vector<hourly_obs> fetch(http_client& http, double lat, double lon);
};
