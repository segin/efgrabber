/*
 * src/cookie.cpp - Implementation of custom cookie management
 * Copyright (c) 2026 Kirn Gill II
 * SPDX-License-Identifier: MIT
 * See LICENSE file for full license text.
 */

#include "efgrabber/cookie.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <cstring>

namespace efgrabber {

// --- Cookie Implementation ---

Cookie::Cookie(std::string key, std::string value, std::string domain, bool secure, time_t expiry)
    : key_(std::move(key)), value_(std::move(value)), domain_(std::move(domain)),
      secure_(secure), expiry_(expiry) {
}

bool Cookie::is_expired() const {
    return std::time(nullptr) > expiry_;
}

bool Cookie::matches(const std::string& request_domain, bool secure_req) const {
    if (is_expired()) return false;
    if (secure_ && !secure_req) return false;

    // Domain matching
    // 1. Exact match
    if (domain_ == request_domain) return true;

    // 2. Suffix match (e.g., domain=".justice.gov" matches "www.justice.gov")
    if (domain_.length() < request_domain.length()) {
        size_t diff = request_domain.length() - domain_.length();
        if (request_domain.compare(diff, domain_.length(), domain_) == 0) {
            // Ensure we match a dot boundary if domain_ doesn't start with dot
            if (domain_[0] != '.') {
                return request_domain[diff - 1] == '.';
            }
            return true;
        }
    }

    return false;
}

std::string Cookie::to_string() const {
    return key_ + "=" + value_;
}

// --- CookieJar Implementation ---

CookieJar::CookieJar() {}

CookieJar::~CookieJar() {
    stop_reaper();
}

void CookieJar::add_cookie(const Cookie& cookie) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto& list = cookies_[cookie.domain()];

    // Update existing if key matches
    auto it = std::find_if(list.begin(), list.end(), [&](const Cookie& c) {
        return c.key() == cookie.key();
    });

    if (it != list.end()) {
        *it = cookie;
    } else {
        list.push_back(cookie);
    }
}

void CookieJar::add_from_header(const std::string& header_line, const std::string& default_domain) {
    // Simple parser for "Set-Cookie: key=value; Expires=...; Domain=...; Secure"
    // Note: robust parsing is complex; this is a basic implementation for the task

    std::string content = header_line;
    if (content.find("Set-Cookie:") == 0) {
        content = content.substr(11);
    }

    // Trim whitespace
    size_t first = content.find_first_not_of(" \t");
    if (first == std::string::npos) return;
    content = content.substr(first);

    std::stringstream ss(content);
    std::string segment;

    std::string key, value;
    std::string domain = default_domain;
    bool secure = false;
    time_t expiry = std::time(nullptr) + 86400; // Default 24h

    bool first_segment = true;
    while (std::getline(ss, segment, ';')) {
        // Trim
        size_t start = segment.find_first_not_of(" \t");
        size_t end = segment.find_last_not_of(" \t");
        if (start == std::string::npos) continue;
        segment = segment.substr(start, end - start + 1);

        size_t eq_pos = segment.find('=');

        if (first_segment) {
            if (eq_pos != std::string::npos) {
                key = segment.substr(0, eq_pos);
                value = segment.substr(eq_pos + 1);
            }
            first_segment = false;
        } else {
            std::string attr_name = (eq_pos != std::string::npos) ? segment.substr(0, eq_pos) : segment;
            std::transform(attr_name.begin(), attr_name.end(), attr_name.begin(), ::tolower);

            if (attr_name == "domain" && eq_pos != std::string::npos) {
                domain = segment.substr(eq_pos + 1);
            } else if (attr_name == "secure") {
                secure = true;
            }
            // Max-Age handling could go here
        }
    }

    if (!key.empty()) {
        add_cookie(Cookie(key, value, domain, secure, expiry));
    }
}

void CookieJar::add_from_cookie_string(const std::string& cookie_string, const std::string& domain) {
    std::stringstream ss(cookie_string);
    std::string segment;
    time_t expiry = std::time(nullptr) + 86400 * 30; // Assume valid for 30 days if manually provided

    while (std::getline(ss, segment, ';')) {
        size_t start = segment.find_first_not_of(" \t");
        size_t end = segment.find_last_not_of(" \t");
        if (start == std::string::npos) continue;
        segment = segment.substr(start, end - start + 1);

        size_t eq_pos = segment.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = segment.substr(0, eq_pos);
            std::string value = segment.substr(eq_pos + 1);

            // Basic heuristic: if domain is secure (justice.gov), mark cookie as secure
            bool secure = (domain.find("justice.gov") != std::string::npos);

            add_cookie(Cookie(key, value, domain, secure, expiry));
        }
    }
}

std::string CookieJar::get_cookies_for_url(const std::string& url) {
    std::string req_domain = extract_domain(url);
    bool secure = is_url_secure(url);

    std::lock_guard<std::mutex> lock(mutex_);
    std::stringstream ss;
    bool first = true;

    // Iterate all lists (since domain matching is loose)
    // For optimization, we could store by domain suffix
    for (const auto& pair : cookies_) {
        const auto& domain_cookies = pair.second;
        for (const auto& cookie : domain_cookies) {
            if (cookie.matches(req_domain, secure)) {
                if (!first) ss << "; ";
                ss << cookie.to_string();
                first = false;
            }
        }
    }

    return ss.str();
}

void CookieJar::cleanup_expired() {
    std::lock_guard<std::mutex> lock(mutex_);
    time_t now = std::time(nullptr);
    int removed_count = 0;

    for (auto it = cookies_.begin(); it != cookies_.end();) {
        auto& list = it->second;
        auto original_size = list.size();

        list.erase(std::remove_if(list.begin(), list.end(), [&](const Cookie& c) {
            return c.expiry() < now;
        }), list.end());

        removed_count += (original_size - list.size());

        if (list.empty()) {
            it = cookies_.erase(it);
        } else {
            ++it;
        }
    }

    if (removed_count > 0) {
        // Logging removed count could go here if we had the logger
    }
}

void CookieJar::start_reaper(int interval_seconds) {
    std::lock_guard<std::mutex> lock(reaper_mutex_);
    if (reaper_running_) return;

    reaper_running_ = true;
    reaper_thread_ = std::thread(&CookieJar::reaper_thread_func, this, interval_seconds);
}

void CookieJar::stop_reaper() {
    {
        std::lock_guard<std::mutex> lock(reaper_mutex_);
        if (!reaper_running_) return;
        reaper_running_ = false;
    }
    reaper_cv_.notify_all();

    if (reaper_thread_.joinable()) {
        reaper_thread_.join();
    }
}

void CookieJar::reaper_thread_func(int interval_seconds) {
    while (reaper_running_) {
        std::unique_lock<std::mutex> lock(reaper_mutex_);
        if (reaper_cv_.wait_for(lock, std::chrono::seconds(interval_seconds),
            [this] { return !reaper_running_; })) {
            break; // Stopped
        }

        cleanup_expired();
    }
}

std::string CookieJar::extract_domain(const std::string& url) const {
    size_t start = url.find("://");
    if (start == std::string::npos) start = 0;
    else start += 3;

    size_t end = url.find('/', start);
    if (end == std::string::npos) end = url.length();

    size_t port_pos = url.find(':', start);
    if (port_pos != std::string::npos && port_pos < end) end = port_pos;

    return url.substr(start, end - start);
}

bool CookieJar::is_url_secure(const std::string& url) const {
    return url.find("https://") == 0;
}

} // namespace efgrabber
