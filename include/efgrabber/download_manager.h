#pragma once

#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <chrono>
#include "efgrabber/common.h"
#include "efgrabber/database.h"
#include "efgrabber/downloader.h"
#include "efgrabber/scraper.h"
#include "efgrabber/thread_pool.h"

namespace efgrabber {

// Operation mode
enum class OperationMode {
    SCRAPER,        // Scrape index pages and download found PDFs
    BRUTE_FORCE,    // Iterate through all possible file IDs
    HYBRID          // Combine both modes
};

// Callbacks for GUI updates
struct DownloadCallbacks {
    std::function<void(const DownloadStats&)> on_stats_update;
    std::function<void(const std::string& message)> on_log_message;
    std::function<void(const std::string& file_id, DownloadStatus status)> on_file_status_change;
    std::function<void(int page_number, int pdf_count)> on_page_scraped;
    std::function<void()> on_complete;
    std::function<void(const std::string& error)> on_error;
};

class DownloadManager {
public:
    DownloadManager(const std::string& db_path, const std::string& download_dir);
    ~DownloadManager();

    // Non-copyable
    DownloadManager(const DownloadManager&) = delete;
    DownloadManager& operator=(const DownloadManager&) = delete;

    // Initialize the manager
    bool initialize();

    // Start downloading
    void start(const DataSetConfig& config, OperationMode mode);
    void start_download_only(const DataSetConfig& config);  // Only download, don't scrape or brute force

    // Stop downloading
    void stop();

    // Pause/resume
    void pause();
    void resume();

    // Check status
    bool is_running() const { return running_.load(); }
    bool is_paused() const { return paused_.load(); }

    // Get current statistics
    DownloadStats get_stats() const;

    // Set callbacks for progress updates
    void set_callbacks(const DownloadCallbacks& callbacks);

    // Configuration
    void set_max_concurrent_downloads(int max);
    void set_max_concurrent_scrapes(int max);
    void set_retry_attempts(int attempts);
    void set_cookie_file(const std::string& cookie_file);
    void set_cookie_string(const std::string& cookies);  // Direct cookie string

    // Add file to download queue (for browser-based scraping)
    void add_file_to_queue(const std::string& file_id, const std::string& url, const std::string& local_path);
    void add_files_to_queue(const std::vector<std::tuple<std::string, std::string, std::string>>& files);

private:
    // Worker methods
    void scraper_worker();
    void brute_force_worker();
    void download_worker();
    void stats_worker();

    // Scraping
    void scrape_page(int page_number);
    void queue_pdf_for_download(const PdfLink& pdf);

    // Downloading
    void download_file(const FileRecord& file);
    std::string get_local_path(const std::string& file_id) const;

    // Helper methods
    void log(const std::string& message);
    void update_stats();

    // Core components
    std::unique_ptr<Database> db_;
    std::unique_ptr<ThreadPool> download_pool_;
    std::unique_ptr<ThreadPool> scrape_pool_;
    std::unique_ptr<Scraper> scraper_;

    // Configuration
    std::string db_path_;
    std::string download_dir_;
    DataSetConfig current_config_;
    OperationMode current_mode_;
    int max_concurrent_downloads_ = MAX_CONCURRENT_DOWNLOADS;
    int max_concurrent_scrapes_ = MAX_CONCURRENT_PAGE_SCRAPES;
    int max_retry_attempts_ = MAX_RETRY_ATTEMPTS;
    std::string cookie_file_;
    std::string cookie_string_;  // Direct cookie string from browser

    // State
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> stop_requested_{false};

    // Statistics
    mutable std::mutex stats_mutex_;
    DownloadStats stats_;
    std::chrono::steady_clock::time_point start_time_;
    std::atomic<int64_t> active_downloads_{0};
    std::atomic<int64_t> bytes_this_session_{0};

    // Brute force state
    std::atomic<uint64_t> brute_force_current_{0};

    // Callbacks
    DownloadCallbacks callbacks_;
    mutable std::mutex callback_mutex_;

    // Threads
    std::thread scraper_thread_;
    std::thread brute_force_thread_;
    std::thread stats_thread_;

    // Synchronization
    std::mutex pause_mutex_;
    std::condition_variable pause_cv_;
};

} // namespace efgrabber
