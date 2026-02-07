#include "efgrabber/database.h"
#include <sqlite3.h>
#include <stdexcept>
#include <sstream>
#include <cstring>

namespace efgrabber {

Database::Database(const std::string& db_path) : db_path_(db_path) {
    int rc = sqlite3_open(db_path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::string error = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("Failed to open database: " + error);
    }

    // Enable WAL mode for better concurrent access
    execute("PRAGMA journal_mode=WAL");
    execute("PRAGMA synchronous=NORMAL");
    execute("PRAGMA cache_size=10000");
    execute("PRAGMA temp_store=MEMORY");
}

Database::~Database() {
    close();
}

Database::Database(Database&& other) noexcept
    : db_(other.db_), db_path_(std::move(other.db_path_)),
      last_error_(std::move(other.last_error_)) {
    other.db_ = nullptr;
}

Database& Database::operator=(Database&& other) noexcept {
    if (this != &other) {
        close();
        db_ = other.db_;
        db_path_ = std::move(other.db_path_);
        last_error_ = std::move(other.last_error_);
        other.db_ = nullptr;
    }
    return *this;
}

void Database::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool Database::execute(const std::string& sql) {
    char* error_msg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error_msg);
    if (rc != SQLITE_OK) {
        last_error_ = error_msg ? error_msg : "Unknown error";
        sqlite3_free(error_msg);
        return false;
    }
    return true;
}

bool Database::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);

    const char* schema = R"(
        CREATE TABLE IF NOT EXISTS files (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            data_set INTEGER NOT NULL,
            file_id TEXT NOT NULL,
            url TEXT NOT NULL,
            local_path TEXT,
            status TEXT NOT NULL DEFAULT 'PENDING',
            file_size INTEGER DEFAULT 0,
            retry_count INTEGER DEFAULT 0,
            error_message TEXT,
            created_at TEXT DEFAULT (datetime('now')),
            updated_at TEXT DEFAULT (datetime('now')),
            UNIQUE(data_set, file_id)
        );

        CREATE INDEX IF NOT EXISTS idx_files_status ON files(status);
        CREATE INDEX IF NOT EXISTS idx_files_data_set ON files(data_set);
        CREATE INDEX IF NOT EXISTS idx_files_file_id ON files(file_id);

        CREATE TABLE IF NOT EXISTS pages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            data_set INTEGER NOT NULL,
            page_number INTEGER NOT NULL,
            scraped INTEGER DEFAULT 0,
            pdf_count INTEGER DEFAULT 0,
            scraped_at TEXT,
            UNIQUE(data_set, page_number)
        );

        CREATE INDEX IF NOT EXISTS idx_pages_data_set ON pages(data_set);
        CREATE INDEX IF NOT EXISTS idx_pages_scraped ON pages(scraped);

        CREATE TABLE IF NOT EXISTS progress (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            data_set INTEGER NOT NULL UNIQUE,
            brute_force_current INTEGER DEFAULT 0,
            updated_at TEXT DEFAULT (datetime('now'))
        );
    )";

    return execute(schema);
}

bool Database::add_file(const FileRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT OR IGNORE INTO files (data_set, file_id, url, local_path, status)
        VALUES (?, ?, ?, ?, ?)
    )";

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        last_error_ = sqlite3_errmsg(db_);
        return false;
    }

    sqlite3_bind_int(stmt, 1, record.data_set);
    sqlite3_bind_text(stmt, 2, record.file_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, record.url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, record.local_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, status_to_string(record.status), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        last_error_ = sqlite3_errmsg(db_);
        return false;
    }

    return true;
}

bool Database::add_files_batch(const std::vector<FileRecord>& records) {
    if (records.empty()) return true;

    std::lock_guard<std::mutex> lock(mutex_);

    if (!execute("BEGIN TRANSACTION")) return false;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT OR IGNORE INTO files (data_set, file_id, url, local_path, status)
        VALUES (?, ?, ?, ?, ?)
    )";

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        last_error_ = sqlite3_errmsg(db_);
        execute("ROLLBACK");
        return false;
    }

    for (const auto& record : records) {
        sqlite3_reset(stmt);
        sqlite3_bind_int(stmt, 1, record.data_set);
        sqlite3_bind_text(stmt, 2, record.file_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, record.url.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, record.local_path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, status_to_string(record.status), -1, SQLITE_STATIC);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            last_error_ = sqlite3_errmsg(db_);
            sqlite3_finalize(stmt);
            execute("ROLLBACK");
            return false;
        }
    }

    sqlite3_finalize(stmt);
    return execute("COMMIT");
}

bool Database::update_file_status(int64_t id, DownloadStatus status,
                                  const std::string& error_msg, int64_t file_size) {
    std::lock_guard<std::mutex> lock(mutex_);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        UPDATE files SET status = ?, error_message = ?, file_size = ?,
                        updated_at = datetime('now')
        WHERE id = ?
    )";

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        last_error_ = sqlite3_errmsg(db_);
        return false;
    }

    sqlite3_bind_text(stmt, 1, status_to_string(status), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, error_msg.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, file_size);
    sqlite3_bind_int64(stmt, 4, id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

bool Database::update_file_status_by_file_id(const std::string& file_id, int data_set,
                                              DownloadStatus status,
                                              const std::string& error_msg,
                                              int64_t file_size) {
    std::lock_guard<std::mutex> lock(mutex_);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        UPDATE files SET status = ?, error_message = ?, file_size = ?,
                        updated_at = datetime('now')
        WHERE file_id = ? AND data_set = ?
    )";

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        last_error_ = sqlite3_errmsg(db_);
        return false;
    }

    sqlite3_bind_text(stmt, 1, status_to_string(status), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, error_msg.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, file_size);
    sqlite3_bind_text(stmt, 4, file_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, data_set);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

std::optional<FileRecord> Database::get_file(int64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        SELECT id, data_set, file_id, url, local_path, status, file_size,
               retry_count, error_message
        FROM files WHERE id = ?
    )";

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_int64(stmt, 1, id);
    rc = sqlite3_step(stmt);

    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return std::nullopt;
    }

    FileRecord record;
    record.id = sqlite3_column_int64(stmt, 0);
    record.data_set = sqlite3_column_int(stmt, 1);
    record.file_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    record.url = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    const char* local_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    record.local_path = local_path ? local_path : "";
    record.status = string_to_status(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)));
    record.file_size = sqlite3_column_int64(stmt, 6);
    record.retry_count = sqlite3_column_int(stmt, 7);
    const char* error = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
    record.error_message = error ? error : "";

    sqlite3_finalize(stmt);
    return record;
}

std::optional<FileRecord> Database::get_file_by_file_id(const std::string& file_id, int data_set) {
    std::lock_guard<std::mutex> lock(mutex_);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        SELECT id, data_set, file_id, url, local_path, status, file_size,
               retry_count, error_message
        FROM files WHERE file_id = ? AND data_set = ?
    )";

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, file_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, data_set);
    rc = sqlite3_step(stmt);

    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return std::nullopt;
    }

    FileRecord record;
    record.id = sqlite3_column_int64(stmt, 0);
    record.data_set = sqlite3_column_int(stmt, 1);
    record.file_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    record.url = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    const char* local_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    record.local_path = local_path ? local_path : "";
    record.status = string_to_status(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)));
    record.file_size = sqlite3_column_int64(stmt, 6);
    record.retry_count = sqlite3_column_int(stmt, 7);
    const char* error = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
    record.error_message = error ? error : "";

    sqlite3_finalize(stmt);
    return record;
}

std::vector<FileRecord> Database::get_pending_files(int limit) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<FileRecord> result;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        SELECT id, data_set, file_id, url, local_path, status, file_size,
               retry_count, error_message
        FROM files WHERE status = 'PENDING' LIMIT ?
    )";

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return result;
    }

    sqlite3_bind_int(stmt, 1, limit);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        FileRecord record;
        record.id = sqlite3_column_int64(stmt, 0);
        record.data_set = sqlite3_column_int(stmt, 1);
        record.file_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        record.url = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const char* local_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        record.local_path = local_path ? local_path : "";
        record.status = string_to_status(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)));
        record.file_size = sqlite3_column_int64(stmt, 6);
        record.retry_count = sqlite3_column_int(stmt, 7);
        const char* error = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        record.error_message = error ? error : "";
        result.push_back(std::move(record));
    }

    sqlite3_finalize(stmt);
    return result;
}

std::vector<FileRecord> Database::get_failed_files(int max_retries, int limit) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<FileRecord> result;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        SELECT id, data_set, file_id, url, local_path, status, file_size,
               retry_count, error_message
        FROM files WHERE status = 'FAILED' AND retry_count < ? LIMIT ?
    )";

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return result;
    }

    sqlite3_bind_int(stmt, 1, max_retries);
    sqlite3_bind_int(stmt, 2, limit);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        FileRecord record;
        record.id = sqlite3_column_int64(stmt, 0);
        record.data_set = sqlite3_column_int(stmt, 1);
        record.file_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        record.url = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const char* local_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        record.local_path = local_path ? local_path : "";
        record.status = string_to_status(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)));
        record.file_size = sqlite3_column_int64(stmt, 6);
        record.retry_count = sqlite3_column_int(stmt, 7);
        const char* error = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        record.error_message = error ? error : "";
        result.push_back(std::move(record));
    }

    sqlite3_finalize(stmt);
    return result;
}

bool Database::increment_retry_count(int64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE files SET retry_count = retry_count + 1 WHERE id = ?";

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

bool Database::file_exists(const std::string& file_id, int data_set) {
    std::lock_guard<std::mutex> lock(mutex_);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT 1 FROM files WHERE file_id = ? AND data_set = ? LIMIT 1";

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, file_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, data_set);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_ROW;
}

bool Database::add_page(int data_set, int page_number) {
    std::lock_guard<std::mutex> lock(mutex_);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT OR IGNORE INTO pages (data_set, page_number) VALUES (?, ?)";

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int(stmt, 1, data_set);
    sqlite3_bind_int(stmt, 2, page_number);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

bool Database::add_pages_batch(int data_set, int start_page, int end_page) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!execute("BEGIN TRANSACTION")) return false;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT OR IGNORE INTO pages (data_set, page_number) VALUES (?, ?)";

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        execute("ROLLBACK");
        return false;
    }

    for (int page = start_page; page <= end_page; ++page) {
        sqlite3_reset(stmt);
        sqlite3_bind_int(stmt, 1, data_set);
        sqlite3_bind_int(stmt, 2, page);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            execute("ROLLBACK");
            return false;
        }
    }

    sqlite3_finalize(stmt);
    return execute("COMMIT");
}

bool Database::mark_page_scraped(int data_set, int page_number, int pdf_count) {
    std::lock_guard<std::mutex> lock(mutex_);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        UPDATE pages SET scraped = 1, pdf_count = ?, scraped_at = datetime('now')
        WHERE data_set = ? AND page_number = ?
    )";

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int(stmt, 1, pdf_count);
    sqlite3_bind_int(stmt, 2, data_set);
    sqlite3_bind_int(stmt, 3, page_number);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

std::vector<int> Database::get_unscraped_pages(int data_set, int limit) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<int> result;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        SELECT page_number FROM pages
        WHERE data_set = ? AND scraped = 0
        ORDER BY page_number
        LIMIT ?
    )";

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return result;
    }

    sqlite3_bind_int(stmt, 1, data_set);
    sqlite3_bind_int(stmt, 2, limit);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        result.push_back(sqlite3_column_int(stmt, 0));
    }

    sqlite3_finalize(stmt);
    return result;
}

bool Database::page_exists(int data_set, int page_number) {
    std::lock_guard<std::mutex> lock(mutex_);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT 1 FROM pages WHERE data_set = ? AND page_number = ? LIMIT 1";

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int(stmt, 1, data_set);
    sqlite3_bind_int(stmt, 2, page_number);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_ROW;
}

DownloadStats Database::get_stats(int data_set) {
    std::lock_guard<std::mutex> lock(mutex_);

    DownloadStats stats{};
    sqlite3_stmt* stmt = nullptr;

    // Get page stats
    const char* page_sql = R"(
        SELECT
            COUNT(*) as total,
            SUM(CASE WHEN scraped = 1 THEN 1 ELSE 0 END) as scraped,
            SUM(pdf_count) as pdf_total
        FROM pages WHERE data_set = ?
    )";

    if (sqlite3_prepare_v2(db_, page_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, data_set);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            stats.total_pages = sqlite3_column_int64(stmt, 0);
            stats.pages_scraped = sqlite3_column_int64(stmt, 1);
            stats.total_files_found = sqlite3_column_int64(stmt, 2);
        }
        sqlite3_finalize(stmt);
    }

    // Get file stats
    const char* file_sql = R"(
        SELECT status, COUNT(*) FROM files WHERE data_set = ? GROUP BY status
    )";

    if (sqlite3_prepare_v2(db_, file_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, data_set);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            int64_t count = sqlite3_column_int64(stmt, 1);

            if (strcmp(status, "PENDING") == 0) stats.files_pending = count;
            else if (strcmp(status, "IN_PROGRESS") == 0) stats.files_in_progress = count;
            else if (strcmp(status, "COMPLETED") == 0) stats.files_completed = count;
            else if (strcmp(status, "FAILED") == 0) stats.files_failed = count;
            else if (strcmp(status, "NOT_FOUND") == 0) stats.files_not_found = count;
        }
        sqlite3_finalize(stmt);
    }

    // Get brute force progress
    const char* bf_sql = "SELECT brute_force_current FROM progress WHERE data_set = ?";
    if (sqlite3_prepare_v2(db_, bf_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, data_set);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            stats.brute_force_current = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    return stats;
}

int64_t Database::get_total_files(int data_set) {
    std::lock_guard<std::mutex> lock(mutex_);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT COUNT(*) FROM files WHERE data_set = ?";

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }

    sqlite3_bind_int(stmt, 1, data_set);
    int64_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

int64_t Database::get_completed_files(int data_set) {
    std::lock_guard<std::mutex> lock(mutex_);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT COUNT(*) FROM files WHERE data_set = ? AND status = 'COMPLETED'";

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }

    sqlite3_bind_int(stmt, 1, data_set);
    int64_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

bool Database::set_brute_force_progress(int data_set, uint64_t current_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT INTO progress (data_set, brute_force_current, updated_at)
        VALUES (?, ?, datetime('now'))
        ON CONFLICT(data_set) DO UPDATE SET
            brute_force_current = excluded.brute_force_current,
            updated_at = datetime('now')
    )";

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int(stmt, 1, data_set);
    sqlite3_bind_int64(stmt, 2, static_cast<int64_t>(current_id));
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

uint64_t Database::get_brute_force_progress(int data_set) {
    std::lock_guard<std::mutex> lock(mutex_);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT brute_force_current FROM progress WHERE data_set = ?";

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }

    sqlite3_bind_int(stmt, 1, data_set);
    uint64_t result = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = static_cast<uint64_t>(sqlite3_column_int64(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return result;
}

bool Database::begin_transaction() {
    return execute("BEGIN TRANSACTION");
}

bool Database::commit_transaction() {
    return execute("COMMIT");
}

bool Database::rollback_transaction() {
    return execute("ROLLBACK");
}

void Database::vacuum() {
    execute("VACUUM");
}

} // namespace efgrabber
