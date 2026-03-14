#pragma once
#include "data_structs.h"
#include "http_client.h"
#include <vector>
#include <string>

class metar_fetcher {
public:
    metar_fetcher() = default;
    std::vector<metar_report> fetch(http_client& http, const std::vector<std::string>& stations);
};
