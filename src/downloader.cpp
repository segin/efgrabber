/*
 * downloader.cpp - Implementation of the libcurl-based HTTP downloader
 * Copyright © 2026 Kirn Gill II <segin2005@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "efgrabber/downloader.h"
#include <curl/curl.h>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <chrono>

namespace efgrabber {

// Static members for global init
std::once_flag CurlGlobalInit::init_flag_;
std::unique_ptr<CurlGlobalInit> CurlGlobalInit::instance_;

CurlGlobalInit::CurlGlobalInit() {
    curl_global_init(CURL_GLOBAL_ALL);
}

CurlGlobalInit::~CurlGlobalInit() {
    curl_global_cleanup();
}

CurlGlobalInit& CurlGlobalInit::instance() {
    std::call_once(init_flag_, []() {
        instance_ = std::unique_ptr<CurlGlobalInit>(new CurlGlobalInit());
    });
    return *instance_;
}

// Write callback for memory downloads
static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t real_size = size * nmemb;
    auto* buffer = static_cast<std::vector<char>*>(userp);
    buffer->insert(buffer->end(),
                   static_cast<char*>(contents),
                   static_cast<char*>(contents) + real_size);
    return real_size;
}

// Write callback for file downloads
struct FileWriteData {
    std::ofstream* file;
    Downloader* downloader;
    ProgressCallback progress_cb;
    int64_t downloaded;
    int64_t total;
};

// Internal struct to pass to progress callback
struct ProgressData {
    Downloader* downloader;
    FileWriteData* file_data; // Pointer to file data to update total
};

static size_t file_write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t real_size = size * nmemb;
    auto* data = static_cast<FileWriteData*>(userp);

    if (data->downloader->is_cancelled()) {
        return 0;  // Abort transfer
    }

    data->file->write(static_cast<char*>(contents), real_size);
    if (!data->file->good()) {
        return 0;  // Write error
    }

    data->downloaded += real_size;

    // We delegate progress updates to the progress_callback which has access to dltotal
    // But we still need to check if progress_cb is valid here if we wanted to use it,
    // but without total size it's less useful.

    return real_size;
}

// Progress callback for cancellation and updates
static int progress_callback(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                             curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    // If we passed ProgressData*, use it. If we passed Downloader*, use it just for cancel.
    // The safest way is to ensure we always wrap context if we want progress.
    // However, download() sets clientp to 'this' (Downloader*), while download_to_file()
    // sets it to ProgressData*.
    // We cannot easily differentiate void* types at runtime.
    //
    // Solution: We will rely on the fact that if we set CURLOPT_XFERINFODATA, we know what we set.
    // But this is a static callback function.
    //
    // We will assume that if we are in this callback, we are receiving what we set.
    // BUT we need to know what to cast it to.
    //
    // To solve this properly, we will ALWAYS pass ProgressData structure to XFERINFODATA, even if file_data is null.

    auto* progress_data = static_cast<ProgressData*>(clientp);
    if (progress_data->downloader->is_cancelled()) return 1;

    if (progress_data->file_data && progress_data->file_data->progress_cb) {
        // Update total if known
        if (dltotal > 0) {
            progress_data->file_data->total = dltotal;
        }
        progress_data->file_data->progress_cb(dlnow, dltotal);
    }

    return 0;
}

// Header callback to extract content info
struct HeaderData {
    int64_t content_length = -1;
    std::string content_type;
    std::vector<std::string> set_cookies;
};

static size_t header_callback(char* buffer, size_t size, size_t nitems, void* userdata) {
    size_t real_size = size * nitems;
    auto* header_data = static_cast<HeaderData*>(userdata);

    std::string header(buffer, real_size);

    // Parse Content-Length
    if (header.find("Content-Length:") == 0 || header.find("content-length:") == 0) {
        size_t pos = header.find(':');
        if (pos != std::string::npos) {
            std::string value = header.substr(pos + 1);
            // Trim whitespace
            size_t start = value.find_first_not_of(" \t\r\n");
            size_t end = value.find_last_not_of(" \t\r\n");
            if (start != std::string::npos && end != std::string::npos) {
                try {
                    header_data->content_length = std::stoll(value.substr(start, end - start + 1));
                } catch (...) {
                    header_data->content_length = -1;
                }
            }
        }
    }

    // Parse Content-Type
    if (header.find("Content-Type:") == 0 || header.find("content-type:") == 0) {
        size_t pos = header.find(':');
        if (pos != std::string::npos) {
            std::string value = header.substr(pos + 1);
            size_t start = value.find_first_not_of(" \t\r\n");
            size_t end = value.find_last_not_of(" \t\r\n");
            if (start != std::string::npos && end != std::string::npos) {
                header_data->content_type = value.substr(start, end - start + 1);
            }
        }
    }

    // Capture Set-Cookie headers
    if (header.find("Set-Cookie:") == 0 || header.find("set-cookie:") == 0) {
        // Strip CRLF
        size_t end = header.find_last_not_of("\r\n");
        if (end != std::string::npos) {
            header_data->set_cookies.push_back(header.substr(0, end + 1));
        }
    }

    return real_size;
}

Downloader::Downloader() : cookie_(REQUIRED_COOKIE), user_agent_(USER_AGENT) {
    CurlGlobalInit::instance();  // Ensure global init
    init_curl();
}

Downloader::~Downloader() {
    cleanup_curl();
}

Downloader::Downloader(Downloader&& other) noexcept
    : curl_(other.curl_), cookie_(std::move(other.cookie_)),
      user_agent_(std::move(other.user_agent_)),
      cancelled_(other.cancelled_.load()),
      bytes_downloaded_(other.bytes_downloaded_.load()) {
    other.curl_ = nullptr;
}

Downloader& Downloader::operator=(Downloader&& other) noexcept {
    if (this != &other) {
        cleanup_curl();
        curl_ = other.curl_;
        cookie_ = std::move(other.cookie_);
        user_agent_ = std::move(other.user_agent_);
        cancelled_ = other.cancelled_.load();
        bytes_downloaded_ = other.bytes_downloaded_.load();
        other.curl_ = nullptr;
    }
    return *this;
}

void Downloader::init_curl() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!curl_) {
        curl_ = curl_easy_init();
        if (!curl_) {
            throw std::runtime_error("Failed to initialize CURL handle");
        }
    }
}

void Downloader::cleanup_curl() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (curl_) {
        curl_easy_cleanup(static_cast<CURL*>(curl_));
        curl_ = nullptr;
    }
}

void Downloader::setup_common_options(CURL* curl, const std::string& url) {
    curl_easy_reset(curl);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent_.c_str());

    // Use cookie file if specified, otherwise use cookie string
    if (!cookie_file_.empty()) {
        curl_easy_setopt(curl, CURLOPT_COOKIEFILE, cookie_file_.c_str());
    } else if (!cookie_.empty()) {
        curl_easy_setopt(curl, CURLOPT_COOKIE, cookie_.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);

    // SSL options
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    // Progress/cancellation support
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);

    // Default XFERINFODATA will be overwritten by download/download_to_file
    // to ensure type safety for the callback.

    // Enable TCP keepalive
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 120L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 60L);
}

DownloadResult Downloader::download(const std::string& url, int timeout_seconds) {
    std::lock_guard<std::mutex> lock(mutex_);

    DownloadResult result{};
    result.success = false;

    if (!curl_) {
        result.error_message = "CURL not initialized";
        return result;
    }

    cancelled_ = false;

    CURL* curl = static_cast<CURL*>(curl_);
    setup_common_options(curl, url);

    // Set up ProgressData wrapper for cancellation check (file_data is null)
    ProgressData progress_data{this, nullptr};
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress_data);

    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);  // 5 second connect timeout
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1000L);  // Abort if below 1KB/s
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 10L);     // for more than 10 seconds
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.data);

    HeaderData header_data;
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &header_data);

    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    result.http_code = static_cast<int>(http_code);
    result.content_length = header_data.content_length;
    result.content_type = header_data.content_type;
    result.set_cookie_headers = std::move(header_data.set_cookies);

    if (res != CURLE_OK) {
        result.error_message = curl_easy_strerror(res);
        if (cancelled_) {
            result.error_message = "Download cancelled";
        }
    } else {
        result.success = (http_code >= 200 && http_code < 300);
        if (!result.success) {
            result.error_message = "HTTP error: " + std::to_string(http_code);
        }
        bytes_downloaded_ += result.data.size();
    }

    return result;
}

DownloadResult Downloader::download_to_file(const std::string& url, const std::string& filepath,
                                            ProgressCallback progress_cb, int timeout_seconds) {
    std::lock_guard<std::mutex> lock(mutex_);

    DownloadResult result{};
    result.success = false;

    if (!curl_) {
        result.error_message = "CURL not initialized";
        return result;
    }

    cancelled_ = false;

    std::ofstream file(filepath, std::ios::binary);
    if (!file) {
        result.error_message = "Failed to open file for writing: " + filepath;
        return result;
    }

    CURL* curl = static_cast<CURL*>(curl_);
    setup_common_options(curl, url);

    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);  // 5 second connect timeout
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1000L);  // Abort if below 1KB/s
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 10L);     // for more than 10 seconds

    FileWriteData write_data{&file, this, progress_cb, 0, 0};
    ProgressData progress_data{this, &write_data};
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress_data);

    HeaderData header_data;

    // Do the actual download
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, file_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_data);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &header_data);

    auto transfer_start = std::chrono::steady_clock::now();
    CURLcode res = curl_easy_perform(curl);
    auto transfer_end = std::chrono::steady_clock::now();
    result.download_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        transfer_end - transfer_start).count();

    file.close();

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    result.http_code = static_cast<int>(http_code);
    result.content_length = write_data.downloaded;
    result.expected_length = header_data.content_length;  // From response headers
    result.content_type = header_data.content_type;
    result.set_cookie_headers = std::move(header_data.set_cookies);

    if (res != CURLE_OK) {
        result.error_message = curl_easy_strerror(res);
        if (cancelled_) {
            result.error_message = "Download cancelled";
        }
        // Remove partial file
        std::remove(filepath.c_str());
    } else {
        result.success = (http_code >= 200 && http_code < 300);

        // Verify size if server provided Content-Length
        if (result.success && result.expected_length > 0 &&
            result.content_length != result.expected_length) {
            result.success = false;
            result.error_message = "Size mismatch: expected " +
                std::to_string(result.expected_length) + " bytes, got " +
                std::to_string(result.content_length);
            std::remove(filepath.c_str());
        } else if (!result.success) {
            result.error_message = "HTTP error: " + std::to_string(http_code);
            std::remove(filepath.c_str());
        }
        bytes_downloaded_ += write_data.downloaded;
    }

    return result;
}

DownloadResult Downloader::download_page(const std::string& url, int timeout_seconds) {
    return download(url, timeout_seconds);
}

bool Downloader::url_exists(const std::string& url) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!curl_) return false;

    cancelled_ = false;

    CURL* curl = static_cast<CURL*>(curl_);
    setup_common_options(curl, url);
    // Use dummy progress data for cancellation
    ProgressData progress_data{this, nullptr};
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress_data);

    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);  // HEAD request
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) return false;

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    return http_code == 200;
}

void Downloader::set_cookie(const std::string& cookie) {
    cookie_ = cookie;
}

void Downloader::set_cookie_file(const std::string& cookie_file) {
    cookie_file_ = cookie_file;
}

void Downloader::set_user_agent(const std::string& user_agent) {
    user_agent_ = user_agent;
}

void Downloader::cancel() {
    cancelled_ = true;
}

} // namespace efgrabber
