/*
 * database.h - SQLite database interface for storing file and page records
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
#include <vector>
#include <optional>
#include <functional>
#include <memory>
#include <mutex>
#include "efgrabber/common.h"

struct sqlite3;

namespace efgrabber {

class Database {
public:
    explicit Database(const std::string& db_path);
    ~Database();

    // Non-copyable
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Move semantics
    Database(Database&& other) noexcept;
    Database& operator=(Database&& other) noexcept;

    // Initialize schema
    bool initialize();

    // File operations
    bool add_file(const FileRecord& record);
    bool add_files_batch(const std::vector<FileRecord>& records);
    bool update_file_status(int64_t id, DownloadStatus status,
                           const std::string& error_msg = "",
                           int64_t file_size = 0);
    bool update_file_status_by_file_id(const std::string& file_id, int data_set,
                                       DownloadStatus status,
                                       const std::string& error_msg = "",
                                       int64_t file_size = 0);
    std::optional<FileRecord> get_file(int64_t id);
    std::optional<FileRecord> get_file_by_file_id(const std::string& file_id, int data_set);
    std::vector<FileRecord> get_pending_files(int limit = 100);
    std::vector<FileRecord> get_failed_files(int max_retries = MAX_RETRY_ATTEMPTS, int limit = 100);
    bool increment_retry_count(int64_t id);
    bool file_exists(const std::string& file_id, int data_set);

    // Page operations
    bool add_page(int data_set, int page_number);
    bool add_pages_batch(int data_set, int start_page, int end_page);
    bool mark_page_scraped(int data_set, int page_number, int pdf_count);
    std::optional<PageRecord> get_page(int data_set, int page_number);
    std::vector<int> get_unscraped_pages(int data_set, int limit = 30);
    bool page_exists(int data_set, int page_number);

    // Statistics
    DownloadStats get_stats(int data_set);
    int64_t get_total_files(int data_set);
    int64_t get_completed_files(int data_set);

    // Resume/retry operations
    int reset_in_progress_files(int data_set);  // Reset IN_PROGRESS to PENDING (for crash recovery)
    int reset_failed_files(int data_set);       // Reset FAILED to PENDING (for retry)
    int reset_all_files(int data_set);          // Reset ALL statuses to PENDING (for redownload)
    bool has_existing_work(int data_set);       // Check if there's pending/failed work
    int clear_data_set(int data_set);           // Delete all records for a data set

    // Brute force mode tracking
    bool set_brute_force_progress(int data_set, uint64_t current_id);
    uint64_t get_brute_force_progress(int data_set);

    // Transaction support
    bool begin_transaction();
    bool commit_transaction();
    bool rollback_transaction();

    // Utility
    void vacuum();
    std::string get_last_error() const { return last_error_; }
    // Structured error reporting
    std::optional<ErrorInfo> get_last_error_info() const { return last_error_info_; }

private:
    bool execute(const std::string& sql);
    bool prepare_statements();
    void close();

    sqlite3* db_ = nullptr;
    std::string db_path_;
    std::string last_error_;
    std::optional<ErrorInfo> last_error_info_; // Structured error info
    mutable std::mutex mutex_;
};

} // namespace efgrabber
