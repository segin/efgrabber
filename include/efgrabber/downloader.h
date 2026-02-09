/*
 * Copyright (c) 2026 Kirn Gill II
 * SPDX-License-Identifier: MIT
 * See LICENSE file for full license text.
 */

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include "efgrabber/common.h"

typedef void CURL;

namespace efgrabber {

// Download result
struct DownloadResult {
    bool success;
    int http_code;
    std::string error_message;
    std::vector<char> data;
    int64_t content_length;      // Actual bytes downloaded
    int64_t expected_length;     // Content-Length header from server (0 if not provided)
    std::string content_type;
    std::vector<std::string> set_cookie_headers; // Captured Set-Cookie headers
    int64_t download_time_ms;    // Actual transfer time in milliseconds (wire time)
};

// Progress callback signature
using ProgressCallback = std::function<void(int64_t downloaded, int64_t total)>;

class Downloader {
public:
    Downloader();
    ~Downloader();

    // Non-copyable but movable
    Downloader(const Downloader&) = delete;
    Downloader& operator=(const Downloader&) = delete;
    Downloader(Downloader&& other) noexcept;
    Downloader& operator=(Downloader&& other) noexcept;

    // Download to memory
    DownloadResult download(const std::string& url, int timeout_seconds = DOWNLOAD_TIMEOUT_SECONDS);

    // Download to file
    DownloadResult download_to_file(const std::string& url, const std::string& filepath,
                                    ProgressCallback progress_cb = nullptr,
                                    int timeout_seconds = DOWNLOAD_TIMEOUT_SECONDS);

    // Download HTML page
    DownloadResult download_page(const std::string& url, int timeout_seconds = PAGE_TIMEOUT_SECONDS);

    // Check if URL exists (HEAD request)
    bool url_exists(const std::string& url);

    // Set custom headers
    void set_cookie(const std::string& cookie);
    void set_cookie_file(const std::string& cookie_file);
    void set_user_agent(const std::string& user_agent);

    // Cancel current download
    void cancel();
    bool is_cancelled() const { return cancelled_.load(); }

    // Statistics
    int64_t bytes_downloaded() const { return bytes_downloaded_.load(); }
    void reset_bytes_counter() { bytes_downloaded_ = 0; }

private:
    void init_curl();
    void cleanup_curl();
    void setup_common_options(CURL* curl, const std::string& url);

    CURL* curl_ = nullptr;
    std::string cookie_;
    std::string cookie_file_;
    std::string user_agent_;
    std::atomic<bool> cancelled_{false};
    std::atomic<int64_t> bytes_downloaded_{0};
    mutable std::mutex mutex_;
};

// RAII wrapper for CURL global init
class CurlGlobalInit {
public:
    CurlGlobalInit();
    ~CurlGlobalInit();
    static CurlGlobalInit& instance();
private:
    static std::once_flag init_flag_;
    static std::unique_ptr<CurlGlobalInit> instance_;
};

} // namespace efgrabber
