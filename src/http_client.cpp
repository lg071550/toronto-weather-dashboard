#include "http_client.h"
#include <curl/curl.h>
#include <spdlog/spdlog.h>

namespace {
    size_t write_callback(char* ptr, size_t size, size_t nmemb, std::string* data) {
        size_t total = size * nmemb;
        data->append(ptr, total);
        return total;
    }
} // namespace

http_client::http_client() {
    curl_ = curl_easy_init();
    if (!curl_) {
        throw std::runtime_error("Failed to initialise libcurl handle");
    }
}

http_client::~http_client() {
    if (curl_) {
        curl_easy_cleanup(static_cast<CURL*>(curl_));
    }
}

http_client::http_client(http_client&& other) noexcept
    : curl_(other.curl_) {
    other.curl_ = nullptr;
}

http_client& http_client::operator=(http_client&& other) noexcept {
    if (this != &other) {
        if (curl_) curl_easy_cleanup(static_cast<CURL*>(curl_));
        curl_ = other.curl_;
        other.curl_ = nullptr;
    }
    return *this;
}

std::string http_client::get(const std::string& url) {
    CURL* curl = static_cast<CURL*>(curl_);
    std::string response;

    curl_easy_reset(curl);
    curl_easy_setopt(curl, CURLOPT_URL,           url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &response);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        60L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,     "TorontoWeatherDash/1.0");

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("HTTP GET failed for ") + url + ": " + curl_easy_strerror(res));
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (http_code < 200 || http_code >= 300) {
        throw std::runtime_error("HTTP " + std::to_string(http_code) + " for " + url);
    }

    return response;
}
