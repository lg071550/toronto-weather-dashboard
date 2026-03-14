#pragma once
#include <string>
#include <vector>

class config {
public:
    explicit config(const std::string& path);

    const std::vector<std::string>& metar_stations()    const { return metar_stations_; }
    int nwp_refresh_seconds()                           const { return nwp_refresh_; }
    int metar_refresh_seconds()                         const { return metar_refresh_; }
    int hourly_refresh_seconds()                        const { return hourly_refresh_; }
    double toronto_lat()                                const { return lat_; }
    double toronto_lon()                                const { return lon_; }

private:
    std::vector<std::string> metar_stations_;
    int nwp_refresh_    = 300;
    int metar_refresh_  = 60;
    int hourly_refresh_ = 300;
    double lat_ = 43.6777;
    double lon_ = -79.6248;
};
