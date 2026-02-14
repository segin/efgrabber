/*
 * download_manager.cpp - Implementation of the download manager
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

#include "efgrabber/download_manager.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>

#include <cmath>

namespace fs = std::filesystem;

namespace efgrabber {

// Helper to get cookies with precedence: Jar > String > File
std::string DownloadManager::get_effective_cookies_for_url(const std::string& url) {
    if (cookie_jar_) {
        std::string cookies = cookie_jar_->get_cookies_for_url(url);
        if (!cookies.empty()) {
            return cookies;
        }
    }

    if (!cookie_string_.empty()) {
        return cookie_string_;
    }

    // Note: Cookie file is handled by setting it directly on the downloader if no string cookies exist,
    // or we can read it here. For simplicity and consistency with curl, we might want to just
    // return empty string here and let the caller check cookie_file_ if this returns empty.
    return "";
}

void DownloadManager::scraper_worker() {
    // First, detect the actual number of pages by probing
    int detected_max_page = -1;

    // Binary search to find max page
    int low = 0;
    int high = 100000;  // Start with a high upper bound

    while (low <= high && !stop_requested_) {
        int mid = low + (high - low) / 2;
        std::string url = scraper_->build_page_url(mid);

        Downloader probe_downloader;
        std::string cookies = get_effective_cookies_for_url(url);
        if (!cookies.empty()) {
            probe_downloader.set_cookie(cookies);
        } else if (!cookie_file_.empty()) {
            probe_downloader.set_cookie_file(cookie_file_);
        }

        auto result = probe_downloader.download_page(url);

        if (cookie_jar_ && !result.set_cookie_headers.empty()) {
            for (const auto& header : result.set_cookie_headers) {
                 cookie_jar_->add_from_header(header, TARGET_DOMAIN);
            }
        }

        if (result.http_code == 200 && !result.data.empty()) {
            // Check if it's a valid page with content
            std::string content(result.data.begin(), result.data.end());
            if (content.find("EFTA") != std::string::npos ||
                content.find(".pdf") != std::string::npos) {
                detected_max_page = mid;
                low = mid + 1;
            } else {
                high = mid - 1;
            }
        } else if (result.http_code == 404) {
            high = mid - 1;
        } else {
            // Error, try again or skip
            high = mid - 1;
        }
    }

    if (detected_max_page < 0) {
        detected_max_page = current_config_.max_page_index;
    }

    // Add all pages to database
    db_->add_pages_batch(current_config_.id, 0, detected_max_page);

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_pages = detected_max_page + 1;
    }

    // Scrape pages
    while (!stop_requested_) {
        // Check for pause
        {
            std::unique_lock<std::mutex> lock(pause_mutex_);
            pause_cv_.wait(lock, [this] { return !paused_ || stop_requested_; });
        }

        if (stop_requested_) break;

        // Get batch of unscraped pages
        auto pages = db_->get_unscraped_pages(current_config_.id, max_concurrent_scrapes_);

        if (pages.empty()) break;

        // Submit scraping tasks
        std::vector<std::future<void>> futures;
        for (int page : pages) {
            futures.push_back(scrape_pool_->submit([this, page] {
                scrape_page(page);
            }));
        }

        // Wait for all to complete
        for (auto& f : futures) {
            try {
                f.get();
            } catch (const std::exception&) {
            }
        }
    }
}

void DownloadManager::brute_force_worker() {
    // Resume from last position
    uint64_t start_id = db_->get_brute_force_progress(current_config_.id);
    if (start_id == 0 || start_id < current_config_.first_file_id) {
        start_id = current_config_.first_file_id;
    }

    brute_force_current_ = start_id;

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.brute_force_current = start_id;
    }

    const size_t batch_size = 1000;  // Process in batches
    std::vector<FileRecord> batch;
    batch.reserve(batch_size);

    for (uint64_t id = start_id; id <= current_config_.last_file_id && !stop_requested_; ++id) {
        // Check for pause
        {
            std::unique_lock<std::mutex> lock(pause_mutex_);
            pause_cv_.wait(lock, [this] { return !paused_ || stop_requested_; });
        }

        if (stop_requested_) break;

        std::string file_id = scraper_->format_file_id(id);

        // Skip if already in database
        if (!db_->file_exists(file_id, current_config_.id)) {
            FileRecord record;
            record.data_set = current_config_.id;
            record.file_id = file_id;
            record.url = scraper_->build_file_url(file_id);
            record.local_path = get_local_path(file_id);
            record.status = DownloadStatus::PENDING;

            batch.push_back(std::move(record));
        }

        brute_force_current_ = id;

        // Commit batch
        if (batch.size() >= batch_size) {
            db_->add_files_batch(batch);
            db_->set_brute_force_progress(current_config_.id, id);
            batch.clear();

            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.brute_force_current = id;
            }
        }
    }

    // Commit remaining
    if (!batch.empty()) {
        db_->add_files_batch(batch);
        db_->set_brute_force_progress(current_config_.id, brute_force_current_.load());
    }
}

void DownloadManager::download_worker() {
    std::cerr << "[DEBUG] download_worker: Started" << std::endl;

    while (!stop_requested_) {
        // Check for pause
        {
            std::unique_lock<std::mutex> lock(pause_mutex_);
            pause_cv_.wait(lock, [this] { return !paused_ || stop_requested_; });
        }

        if (stop_requested_) break;

        int max_downloads = max_concurrent_downloads_.load();

        // Check if we have capacity
        while (active_downloads_.load() >= max_downloads && !stop_requested_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            max_downloads = max_concurrent_downloads_.load();  // Re-check in case it changed
        }

        if (stop_requested_) break;

        // Get pending files
        int want = max_downloads - active_downloads_.load();
        auto files = db_->get_pending_files(want);
        std::cerr << "[DEBUG] download_worker: Requested " << want << " pending files, got " << files.size() << std::endl;

        if (files.empty()) {
            // Check for failed files to retry with S-curve backoff
            auto failed = db_->get_failed_files(max_retry_attempts_, 100);
            auto now = std::chrono::system_clock::now();

            for (const auto& f : failed) {
                int64_t wait_sec = calculate_s_curve_backoff(f.retry_count);
                auto ready_time = f.updated_at + std::chrono::seconds(wait_sec);

                if (now >= ready_time) {
                    files.push_back(f);
                    if (static_cast<int>(files.size()) >= want) break;
                }
            }
        }

        if (files.empty()) {
            // No work to do, check if we should wait or exit
            int64_t active = active_downloads_.load();

            // If downloads are still in progress, wait for them
            if (active > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            // If external scraping is active, wait for more files
            if (external_scraping_active_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }

            // Check if scraper/brute force workers are still running
            bool scraper_done = !scraper_thread_.joinable() ||
                               current_mode_ == OperationMode::BRUTE_FORCE;
            bool bf_done = !brute_force_thread_.joinable() ||
                          current_mode_ == OperationMode::SCRAPER;

            if (!scraper_done || !bf_done) {
                // Workers still running, wait for them to add more files
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }

            // Double-check: query database one more time before exiting
            auto db_stats = db_->get_stats(current_config_.id);
            std::cerr << "[DEBUG] download_worker exit check: pending=" << db_stats.files_pending
                      << " in_progress=" << db_stats.files_in_progress
                      << " completed=" << db_stats.files_completed
                      << " failed=" << db_stats.files_failed << std::endl;
            if (db_stats.files_pending > 0 || db_stats.files_in_progress > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            break;
        }

        // Submit download tasks
        for (const auto& file : files) {
            if (stop_requested_) break;

            // Mark as in progress
            db_->update_file_status(file.id, DownloadStatus::IN_PROGRESS);
            active_downloads_++;

            download_pool_->submit_detached([this, file]() {
                download_file(file);
                active_downloads_--;
            });
        }
    }

    // Signal completion if we exited normally (not stopped)
    if (!stop_requested_) {
        running_ = false;
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (callbacks_.on_complete) {
            callbacks_.on_complete();
        }
    }
}

void DownloadManager::stats_worker() {
    while (!stop_requested_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        if (stop_requested_) break;

        update_stats();
    }
}

void DownloadManager::scrape_page(int page_number) {
    Downloader downloader;
    std::string url = scraper_->build_page_url(page_number);

    // Prefer cookies from jar (which includes initial string + updates),
    // fallback to static string only if jar is empty/failed
    std::string cookies = get_effective_cookies_for_url(url);
    if (!cookies.empty()) {
         downloader.set_cookie(cookies);
    } else if (!cookie_file_.empty()) {
         downloader.set_cookie_file(cookie_file_);
    }

    auto result = downloader.download_page(url);

    if (cookie_jar_ && !result.set_cookie_headers.empty()) {
        for (const auto& header : result.set_cookie_headers) {
             // Default domain is target domain from config if not specified
             cookie_jar_->add_from_header(header, TARGET_DOMAIN);
        }
    }

    if (!result.success) {
        return;
    }

    std::string html(result.data.begin(), result.data.end());
    auto pdf_links = scraper_->extract_pdf_links(html);

    // Add PDFs to database
    std::vector<FileRecord> records;
    for (const auto& pdf : pdf_links) {
        FileRecord record;
        record.data_set = current_config_.id;
        record.file_id = pdf.file_id;
        record.url = pdf.url;
        record.local_path = get_local_path(pdf.file_id);
        record.status = DownloadStatus::PENDING;
        records.push_back(std::move(record));
    }

    db_->add_files_batch(records);
    db_->mark_page_scraped(current_config_.id, page_number, static_cast<int>(pdf_links.size()));

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.pages_scraped++;
        stats_.total_files_found += pdf_links.size();
    }

    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (callbacks_.on_page_scraped) {
            callbacks_.on_page_scraped(page_number, static_cast<int>(pdf_links.size()));
        }
    }
}

void DownloadManager::download_file(const FileRecord& file) {
    try {
        // Check if file already exists locally and has content
        if (!overwrite_existing_ && fs::exists(file.local_path) && fs::file_size(file.local_path) > 0) {
            db_->update_file_status(file.id, DownloadStatus::SKIPPED);
            return;
        }

        // Create directory structure
        fs::path filepath(file.local_path);
        fs::create_directories(filepath.parent_path());

        // Track when this download starts for active transfer time
        auto download_start = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(transfer_time_mutex_);
            if (!any_download_active_.load()) {
                first_active_time_ = download_start;
                any_download_active_.store(true);
            }
        }

        Downloader downloader;
        // Prefer cookies from jar
        std::string cookies = get_effective_cookies_for_url(file.url);
        if (!cookies.empty()) {
             downloader.set_cookie(cookies);
        } else if (!cookie_file_.empty()) {
             downloader.set_cookie_file(cookie_file_);
        }

        auto result = downloader.download_to_file(file.url, file.local_path);

        if (cookie_jar_ && !result.set_cookie_headers.empty()) {
            for (const auto& header : result.set_cookie_headers) {
                 cookie_jar_->add_from_header(header, TARGET_DOMAIN);
            }
        }

        // Track when this download ends
        auto download_end = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(transfer_time_mutex_);
            last_active_time_ = download_end;
            // Update active transfer wall time (time from first active to now)
            active_transfer_wall_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                last_active_time_ - first_active_time_).count();
        }

        if (result.http_code == 404) {
            // Delete empty file if created
            if (fs::exists(file.local_path)) {
                fs::remove(file.local_path);
            }
            db_->update_file_status(file.id, DownloadStatus::NOT_FOUND, "404 Not Found");
        } else if (result.http_code == 403 || result.http_code == 429) {
            // Forbidden or rate limited - anti-bot triggered
            if (fs::exists(file.local_path)) {
                fs::remove(file.local_path);
            }
            db_->increment_retry_count(file.id);
            db_->update_file_status(file.id, DownloadStatus::FAILED,
                "Blocked: HTTP " + std::to_string(result.http_code));

            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (callbacks_.on_file_status_change) {
                callbacks_.on_file_status_change(file.file_id, DownloadStatus::FAILED);
            }
        } else if (result.success && result.content_length > 0) {
            // Success - file downloaded (content type not validated per user request)
            bytes_this_session_ += result.content_length;
            wire_time_ms_ += result.download_time_ms;
            db_->update_file_status(file.id, DownloadStatus::COMPLETED, "", result.content_length);

            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (callbacks_.on_file_status_change) {
                callbacks_.on_file_status_change(file.file_id, DownloadStatus::COMPLETED);
            }
        } else if (result.success && result.content_length == 0) {
            // Empty response - delete and mark not found
            if (fs::exists(file.local_path)) {
                fs::remove(file.local_path);
            }
            db_->update_file_status(file.id, DownloadStatus::NOT_FOUND, "Empty response");
        } else {
            // Download failed - delete empty file
            if (fs::exists(file.local_path)) {
                fs::remove(file.local_path);
            }
            db_->increment_retry_count(file.id);
            db_->update_file_status(file.id, DownloadStatus::FAILED, result.error_message);

            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (callbacks_.on_file_status_change) {
                callbacks_.on_file_status_change(file.file_id, DownloadStatus::FAILED);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] download_file exception for " << file.file_id << ": " << e.what() << std::endl;
        try {
            db_->update_file_status(file.id, DownloadStatus::FAILED, std::string("Exception: ") + e.what());
        } catch (...) {
            // Ignore nested exceptions
        }
    } catch (...) {
        std::cerr << "[ERROR] download_file unknown exception for " << file.file_id << std::endl;
        try {
            db_->update_file_status(file.id, DownloadStatus::FAILED, "Unknown exception");
        } catch (...) {
            // Ignore nested exceptions
        }
    }
}

std::string DownloadManager::get_local_path(const std::string& file_id) const {
    // Organize into subdirectories based on ID to avoid too many files in one folder
    // e.g., EFTA02205655 -> downloads/DataSet11/022/EFTA02205655.pdf
    std::string subdir;
    if (file_id.length() >= 7) {
        subdir = file_id.substr(4, 3);  // Extract first 3 digits of number
    } else {
        subdir = "misc";
    }

    fs::path path = fs::path(download_dir_) / ("DataSet" + std::to_string(current_config_.id)) /
                    subdir / (file_id + ".pdf");
    return path.string();
}

void DownloadManager::update_stats() {
    auto db_stats = db_->get_stats(current_config_.id);

    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - start_time_).count();

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.files_pending = db_stats.files_pending;
        stats_.files_in_progress = active_downloads_.load();
        stats_.files_completed = db_stats.files_completed;
        stats_.files_failed = db_stats.files_failed;
        stats_.files_not_found = db_stats.files_not_found;
        stats_.pages_scraped = db_stats.pages_scraped;
        stats_.total_files_found = db_stats.total_files_found;
        stats_.bytes_downloaded = bytes_this_session_.load();
        stats_.brute_force_current = brute_force_current_.load();

        if (elapsed > 0) {
            stats_.current_speed_bps = bytes_this_session_.load() / elapsed;
        }

        // Wire speed: bytes / wall time during which downloads were active
        // This gives aggregate throughput excluding idle time waiting for scraper
        int64_t active_wall_ms = active_transfer_wall_ms_.load();
        if (active_wall_ms > 0) {
            stats_.wire_speed_bps = (bytes_this_session_.load() * 1000.0) / active_wall_ms;
        } else {
            stats_.wire_speed_bps = 0;
        }
    }

    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (callbacks_.on_stats_update) {
        callbacks_.on_stats_update(stats_);
    }
}

} // namespace efgrabber
