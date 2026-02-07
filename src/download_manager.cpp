#include "efgrabber/download_manager.h"
#include <filesystem>
#include <iostream>
#include <sstream>
#include <iomanip>

namespace fs = std::filesystem;

namespace efgrabber {

DownloadManager::DownloadManager(const std::string& db_path, const std::string& download_dir)
    : db_path_(db_path), download_dir_(download_dir) {
}

DownloadManager::~DownloadManager() {
    stop();
}

bool DownloadManager::initialize() {
    try {
        // Create download directory if it doesn't exist
        fs::create_directories(download_dir_);

        // Initialize database
        db_ = std::make_unique<Database>(db_path_);
        if (!db_->initialize()) {
            log("Failed to initialize database: " + db_->get_last_error());
            return false;
        }

        log("Download manager initialized");
        return true;
    } catch (const std::exception& e) {
        log("Initialization error: " + std::string(e.what()));
        return false;
    }
}

void DownloadManager::start(const DataSetConfig& config, OperationMode mode) {
    if (running_) {
        log("Already running");
        return;
    }

    current_config_ = config;
    current_mode_ = mode;
    running_ = true;
    paused_ = false;
    stop_requested_ = false;

    // Reset stats
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_ = DownloadStats{};
        stats_.brute_force_start = config.first_file_id;
        stats_.brute_force_end = config.last_file_id;
        stats_.start_time = std::chrono::system_clock::now();
    }

    start_time_ = std::chrono::steady_clock::now();
    bytes_this_session_ = 0;

    // Create scraper
    scraper_ = std::make_unique<Scraper>(config);

    // Create thread pools
    download_pool_ = std::make_unique<ThreadPool>(max_concurrent_downloads_);
    scrape_pool_ = std::make_unique<ThreadPool>(max_concurrent_scrapes_);

    log("Starting download for " + config.name);

    // Start worker threads based on mode
    if (mode == OperationMode::SCRAPER || mode == OperationMode::HYBRID) {
        scraper_thread_ = std::thread(&DownloadManager::scraper_worker, this);
    }

    if (mode == OperationMode::BRUTE_FORCE || mode == OperationMode::HYBRID) {
        brute_force_thread_ = std::thread(&DownloadManager::brute_force_worker, this);
    }

    // Start stats update thread
    stats_thread_ = std::thread(&DownloadManager::stats_worker, this);

    // Start download worker
    std::thread(&DownloadManager::download_worker, this).detach();
}

void DownloadManager::stop() {
    if (!running_) return;

    stop_requested_ = true;
    paused_ = false;
    pause_cv_.notify_all();

    // Wait for threads to finish
    if (scraper_thread_.joinable()) {
        scraper_thread_.join();
    }
    if (brute_force_thread_.joinable()) {
        brute_force_thread_.join();
    }
    if (stats_thread_.joinable()) {
        stats_thread_.join();
    }

    // Shutdown thread pools
    if (download_pool_) {
        download_pool_->shutdown();
    }
    if (scrape_pool_) {
        scrape_pool_->shutdown();
    }

    running_ = false;
    log("Download stopped");

    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (callbacks_.on_complete) {
        callbacks_.on_complete();
    }
}

void DownloadManager::pause() {
    if (!running_ || paused_) return;
    paused_ = true;
    log("Download paused");
}

void DownloadManager::resume() {
    if (!running_ || !paused_) return;
    paused_ = false;
    pause_cv_.notify_all();
    log("Download resumed");
}

DownloadStats DownloadManager::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void DownloadManager::set_callbacks(const DownloadCallbacks& callbacks) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    callbacks_ = callbacks;
}

void DownloadManager::set_max_concurrent_downloads(int max) {
    max_concurrent_downloads_ = max;
}

void DownloadManager::set_max_concurrent_scrapes(int max) {
    max_concurrent_scrapes_ = max;
}

void DownloadManager::set_retry_attempts(int attempts) {
    max_retry_attempts_ = attempts;
}

void DownloadManager::scraper_worker() {
    log("Scraper worker started");

    // First, detect the actual number of pages by probing
    int detected_max_page = -1;

    // Binary search to find max page
    int low = 0;
    int high = 100000;  // Start with a high upper bound

    Downloader probe_downloader;

    while (low <= high && !stop_requested_) {
        int mid = low + (high - low) / 2;
        std::string url = scraper_->build_page_url(mid);

        auto result = probe_downloader.download_page(url);

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
        log("Failed to detect page count, using config default");
        detected_max_page = current_config_.max_page_index;
    } else {
        log("Detected " + std::to_string(detected_max_page + 1) + " pages");
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

        if (pages.empty()) {
            log("All pages scraped");
            break;
        }

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
            } catch (const std::exception& e) {
                log("Scrape error: " + std::string(e.what()));
            }
        }
    }

    log("Scraper worker finished");
}

void DownloadManager::brute_force_worker() {
    log("Brute force worker started");

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

    log("Brute force starting from " + scraper_->format_file_id(start_id));

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

    log("Brute force worker finished");
}

void DownloadManager::download_worker() {
    log("Download worker started");

    while (!stop_requested_) {
        // Check for pause
        {
            std::unique_lock<std::mutex> lock(pause_mutex_);
            pause_cv_.wait(lock, [this] { return !paused_ || stop_requested_; });
        }

        if (stop_requested_) break;

        // Check if we have capacity
        while (active_downloads_.load() >= max_concurrent_downloads_ && !stop_requested_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (stop_requested_) break;

        // Get pending files
        auto files = db_->get_pending_files(max_concurrent_downloads_ - active_downloads_.load());

        if (files.empty()) {
            // Check for failed files to retry
            files = db_->get_failed_files(max_retry_attempts_, 100);
        }

        if (files.empty()) {
            // No work to do, check if we're done
            auto stats = db_->get_stats(current_config_.id);
            if (stats.files_pending == 0 && stats.files_in_progress == 0) {
                // Check if scraper/brute force workers are done
                bool scraper_done = !scraper_thread_.joinable() ||
                                   current_mode_ == OperationMode::BRUTE_FORCE;
                bool bf_done = !brute_force_thread_.joinable() ||
                              current_mode_ == OperationMode::SCRAPER;

                if (scraper_done && bf_done) {
                    log("All downloads complete");
                    break;
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
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

    log("Download worker finished");
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

    auto result = downloader.download_page(url);

    if (!result.success) {
        log("Failed to scrape page " + std::to_string(page_number) + ": " + result.error_message);
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
    // Check if file already exists locally
    if (fs::exists(file.local_path) && fs::file_size(file.local_path) > 0) {
        db_->update_file_status(file.id, DownloadStatus::SKIPPED);
        return;
    }

    // Create directory structure
    fs::path filepath(file.local_path);
    fs::create_directories(filepath.parent_path());

    Downloader downloader;
    auto result = downloader.download_to_file(file.url, file.local_path);

    if (result.http_code == 404) {
        db_->update_file_status(file.id, DownloadStatus::NOT_FOUND, "404 Not Found");
    } else if (result.success) {
        bytes_this_session_ += result.content_length;
        db_->update_file_status(file.id, DownloadStatus::COMPLETED, "", result.content_length);

        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (callbacks_.on_file_status_change) {
            callbacks_.on_file_status_change(file.file_id, DownloadStatus::COMPLETED);
        }
    } else {
        db_->increment_retry_count(file.id);
        db_->update_file_status(file.id, DownloadStatus::FAILED, result.error_message);

        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (callbacks_.on_file_status_change) {
            callbacks_.on_file_status_change(file.file_id, DownloadStatus::FAILED);
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

void DownloadManager::log(const std::string& message) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (callbacks_.on_log_message) {
        callbacks_.on_log_message(message);
    }
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
    }

    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (callbacks_.on_stats_update) {
        callbacks_.on_stats_update(stats_);
    }
}

} // namespace efgrabber
