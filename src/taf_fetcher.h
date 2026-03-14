#pragma once
#include "data_structs.h"
#include "http_client.h"
#include <vector>
#include <string>

class taf_fetcher {
public:
    taf_fetcher() = default;
    std::vector<taf_forecast> fetch(http_client& http, const std::vector<std::string>& stations);
};
