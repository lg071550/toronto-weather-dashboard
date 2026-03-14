#pragma once
#include "data_structs.h"
#include "http_client.h"
#include <vector>

class nwp_fetcher {
public:
    nwp_fetcher() = default;
    std::vector<nwp_model_forecast> fetch(http_client& http, double lat, double lon);
};
