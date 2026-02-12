/*
 * cookie.h - Custom cookie management
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

#pragma once

#include <string>
#include <ctime>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <thread>
#include <condition_variable>

namespace efgrabber {

class Cookie {
public:
    Cookie(std::string key, std::string value, std::string domain, bool secure, time_t expiry);

    // Getters
    const std::string& key() const { return key_; }
    const std::string& value() const { return value_; }
    const std::string& domain() const { return domain_; }
    bool is_secure() const { return secure_; }
    time_t expiry() const { return expiry_; }

    // Check if expired
    bool is_expired() const;

    // Check if valid for a given domain and scheme
    bool matches(const std::string& domain, bool secure_req) const;

    // Format for HTTP header (Cookie: key=value)
    std::string to_string() const;

private:
    std::string key_;
    std::string value_;
    std::string domain_;
    bool secure_;
    time_t expiry_;
};

class CookieJar {
public:
    CookieJar();
    ~CookieJar();

    // Add or update a cookie
    void add_cookie(const Cookie& cookie);

    // Add from raw header string (Set-Cookie: ...)
    void add_from_header(const std::string& header_line, const std::string& default_domain);

    // Add from standard cookie string (key=value; key2=value2)
    void add_from_cookie_string(const std::string& cookie_string, const std::string& domain);

    // Get cookies string for a specific request
    std::string get_cookies_for_url(const std::string& url);

    // Manual eviction
    void cleanup_expired();

    // Thread management for automatic reaper
    void start_reaper(int interval_seconds = 60);
    void stop_reaper();

private:
    // Helper to parse domain from URL
    std::string extract_domain(const std::string& url) const;
    bool is_url_secure(const std::string& url) const;
    void reaper_thread_func(int interval_seconds);

    // Storage: domain -> list of cookies
    // This simplifies matching. We could also just use a flat list for small numbers.
    std::unordered_map<std::string, std::vector<Cookie>> cookies_;
    mutable std::mutex mutex_;

    // Reaper state
    std::atomic<bool> reaper_running_{false};
    std::thread reaper_thread_;
    std::condition_variable reaper_cv_;
    std::mutex reaper_mutex_;
};

} // namespace efgrabber
