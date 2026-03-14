#pragma once
#include <string>
#include <stdexcept>

class http_client {
public:
    http_client();
    ~http_client();

    // Non-copyable, movable
    http_client(const http_client&) = delete;
    http_client& operator=(const http_client&) = delete;
    http_client(http_client&&) noexcept;
    http_client& operator=(http_client&&) noexcept;

    std::string get(const std::string& url);

private:
    void* curl_ = nullptr; // CURL handle, typed as void* to avoid header dependency
};
