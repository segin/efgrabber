#include "efgrabber/downloader.h"
#include <curl/curl.h>
#include <cstring>
#include <fstream>
#include <stdexcept>

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

    if (data->progress_cb) {
        data->progress_cb(data->downloaded, data->total);
    }

    return real_size;
}

// Progress callback for cancellation
static int progress_callback(void* clientp, curl_off_t /*dltotal*/, curl_off_t /*dlnow*/,
                             curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    auto* downloader = static_cast<Downloader*>(clientp);
    return downloader->is_cancelled() ? 1 : 0;  // Return non-zero to abort
}

// Header callback to extract content info
struct HeaderData {
    int64_t content_length = -1;
    std::string content_type;
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
                header_data->content_length = std::stoll(value.substr(start, end - start + 1));
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
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this);

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

    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds));
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

    FileWriteData write_data{&file, this, progress_cb, 0, 0};

    // First do a HEAD request to get content length
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    HeaderData header_data;
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &header_data);
    curl_easy_perform(curl);
    write_data.total = header_data.content_length;

    // Now do the actual download
    setup_common_options(curl, url);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, file_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_data);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &header_data);

    CURLcode res = curl_easy_perform(curl);

    file.close();

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    result.http_code = static_cast<int>(http_code);
    result.content_length = write_data.downloaded;
    result.content_type = header_data.content_type;

    if (res != CURLE_OK) {
        result.error_message = curl_easy_strerror(res);
        if (cancelled_) {
            result.error_message = "Download cancelled";
        }
        // Remove partial file
        std::remove(filepath.c_str());
    } else {
        result.success = (http_code >= 200 && http_code < 300);
        if (!result.success) {
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

    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);  // HEAD request
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

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
