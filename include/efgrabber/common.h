#pragma once

#include <string>
#include <cstdint>
#include <chrono>

namespace efgrabber {

// Data set configuration
struct DataSetConfig {
    int id;                     // Data set number (9, 11, etc.)
    std::string name;           // Human-readable name
    std::string base_url;       // Base URL for index pages
    std::string file_url_base;  // Base URL for PDF files
    std::string file_prefix;    // Prefix for file IDs (e.g., "EFTA")
    uint64_t first_file_id;     // First file ID number
    uint64_t last_file_id;      // Last file ID number
    int max_page_index;         // Maximum page index (0-based)
};

// Helper to build data set config dynamically
// Page count is auto-detected at runtime, not hardcoded
inline DataSetConfig make_data_set_config(int id) {
    DataSetConfig config;
    config.id = id;
    config.name = "Data Set " + std::to_string(id);
    config.base_url = "https://www.justice.gov/epstein/doj-disclosures/data-set-" +
                      std::to_string(id) + "-files";
    config.file_url_base = "https://www.justice.gov/epstein/files/DataSet%20" +
                           std::to_string(id) + "/";
    config.file_prefix = "EFTA";
    config.first_file_id = 0;    // Will be determined from scraping or brute force config
    config.last_file_id = 0;     // Will be determined from scraping or brute force config
    config.max_page_index = -1;  // -1 means auto-detect at runtime
    return config;
}

// Known brute force ranges for specific data sets (can be overridden)
// These are starting points; actual ranges may grow over time
inline DataSetConfig get_data_set_11_config() {
    auto config = make_data_set_config(11);
    config.first_file_id = 2205655;
    config.last_file_id = 2730262;
    return config;
}

// All supported data sets (1-12 as of 2025)
constexpr int MIN_DATA_SET = 1;
constexpr int MAX_DATA_SET = 12;

// Download status for a single file
enum class DownloadStatus {
    PENDING,
    IN_PROGRESS,
    COMPLETED,
    FAILED,
    NOT_FOUND,    // 404
    SKIPPED       // Already exists
};

// File record in database
struct FileRecord {
    int64_t id;                 // Database row ID
    int data_set;               // Data set number
    std::string file_id;        // File ID (e.g., "EFTA02205655")
    std::string url;            // Full download URL
    std::string local_path;     // Local file path
    DownloadStatus status;
    int64_t file_size;          // File size in bytes (0 if unknown)
    int retry_count;            // Number of retry attempts
    std::string error_message;  // Last error message if failed
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;
};

// Page record in database
struct PageRecord {
    int64_t id;
    int data_set;
    int page_number;
    bool scraped;
    int pdf_count;              // Number of PDFs found on this page
    std::chrono::system_clock::time_point scraped_at;
};

// Statistics for progress display
struct DownloadStats {
    // Scraper mode stats
    int64_t total_pages;
    int64_t pages_scraped;
    int64_t total_files_found;

    // Download stats
    int64_t files_pending;
    int64_t files_in_progress;
    int64_t files_completed;
    int64_t files_failed;
    int64_t files_not_found;

    // Brute force mode stats
    uint64_t brute_force_current;
    uint64_t brute_force_start;
    uint64_t brute_force_end;

    // Timing
    std::chrono::system_clock::time_point start_time;
    int64_t bytes_downloaded;
    double current_speed_bps;   // Bytes per second
};

// Constants
constexpr int MAX_CONCURRENT_DOWNLOADS = 1000;
constexpr int MAX_CONCURRENT_PAGE_SCRAPES = 30;
constexpr int MAX_RETRY_ATTEMPTS = 3;
constexpr int DOWNLOAD_TIMEOUT_SECONDS = 300;  // 5 minutes
constexpr int PAGE_TIMEOUT_SECONDS = 60;       // 1 minute
constexpr const char* REQUIRED_COOKIE = "justiceGovAgeVerified=true";
constexpr const char* USER_AGENT = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

// Helper to convert DownloadStatus to string
inline const char* status_to_string(DownloadStatus status) {
    switch (status) {
        case DownloadStatus::PENDING: return "PENDING";
        case DownloadStatus::IN_PROGRESS: return "IN_PROGRESS";
        case DownloadStatus::COMPLETED: return "COMPLETED";
        case DownloadStatus::FAILED: return "FAILED";
        case DownloadStatus::NOT_FOUND: return "NOT_FOUND";
        case DownloadStatus::SKIPPED: return "SKIPPED";
        default: return "UNKNOWN";
    }
}

inline DownloadStatus string_to_status(const std::string& str) {
    if (str == "PENDING") return DownloadStatus::PENDING;
    if (str == "IN_PROGRESS") return DownloadStatus::IN_PROGRESS;
    if (str == "COMPLETED") return DownloadStatus::COMPLETED;
    if (str == "FAILED") return DownloadStatus::FAILED;
    if (str == "NOT_FOUND") return DownloadStatus::NOT_FOUND;
    if (str == "SKIPPED") return DownloadStatus::SKIPPED;
    return DownloadStatus::PENDING;
}

} // namespace efgrabber
