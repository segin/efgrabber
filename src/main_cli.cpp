/*
 * src/main_cli.cpp - Main entry point for the CLI application
 * Copyright (c) 2026 Kirn Gill II
 * SPDX-License-Identifier: MIT
 * See LICENSE file for full license text.
 */

#include <iostream>
#include <string>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <getopt.h>

#include "efgrabber/common.h"
#include "efgrabber/download_manager.h"

using namespace efgrabber;

static std::atomic<bool> g_interrupted{false};

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\n[!] Interrupt received, stopping gracefully...\n";
        g_interrupted = true;
    }
}

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [OPTIONS]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -d, --data-set N     Data set number to download (1-12, default: 11)\n";
    std::cout << "  -m, --mode MODE      Download mode: scraper, brute, hybrid (default: scraper)\n";
    std::cout << "  -o, --output DIR     Output directory (default: downloads)\n";
    std::cout << "  -k, --cookies FILE   Netscape cookie file for authentication\n";
    std::cout << "  -c, --concurrent N   Max concurrent downloads (default: 1000)\n";
    std::cout << "  -r, --retries N      Max retry attempts (default: 3)\n";
    std::cout << "  -s, --start ID       Brute force start ID (overrides default)\n";
    std::cout << "  -e, --end ID         Brute force end ID (overrides default)\n";
    std::cout << "  -h, --help           Show this help message\n";
    std::cout << "\nExamples:\n";
    std::cout << "  " << program << " -d 11 -m scraper -k cookies.txt\n";
    std::cout << "  " << program << " -d 9 -m hybrid -c 500\n";
    std::cout << "  " << program << " -d 11 -m brute -s 2205655 -e 2730262\n";
}

std::string format_bytes(int64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_index = 0;
    double value = static_cast<double>(bytes);

    while (value >= 1024.0 && unit_index < 4) {
        value /= 1024.0;
        unit_index++;
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << value << " " << units[unit_index];
    return oss.str();
}

int main(int argc, char** argv) {
    // Default options
    int data_set = 11;
    std::string mode_str = "scraper";
    std::string output_dir = "downloads";
    std::string cookie_file;
    int max_concurrent = 1000;
    int max_retries = 3;
    uint64_t brute_start = 0;
    uint64_t brute_end = 0;

    // Parse command line options
    static struct option long_options[] = {
        {"data-set", required_argument, nullptr, 'd'},
        {"mode", required_argument, nullptr, 'm'},
        {"output", required_argument, nullptr, 'o'},
        {"cookies", required_argument, nullptr, 'k'},
        {"concurrent", required_argument, nullptr, 'c'},
        {"retries", required_argument, nullptr, 'r'},
        {"start", required_argument, nullptr, 's'},
        {"end", required_argument, nullptr, 'e'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "d:m:o:k:c:r:s:e:h", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'd':
                data_set = std::stoi(optarg);
                if (data_set < MIN_DATA_SET || data_set > MAX_DATA_SET) {
                    std::cerr << "Error: Data set must be between " << MIN_DATA_SET
                              << " and " << MAX_DATA_SET << "\n";
                    return 1;
                }
                break;
            case 'm':
                mode_str = optarg;
                break;
            case 'o':
                output_dir = optarg;
                break;
            case 'k':
                cookie_file = optarg;
                break;
            case 'c':
                max_concurrent = std::stoi(optarg);
                if (max_concurrent < 1 || max_concurrent > 10000) {
                    std::cerr << "Error: Concurrent downloads must be between 1 and 10000\n";
                    return 1;
                }
                break;
            case 'r':
                max_retries = std::stoi(optarg);
                break;
            case 's':
                brute_start = std::stoull(optarg);
                break;
            case 'e':
                brute_end = std::stoull(optarg);
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    // Parse mode
    OperationMode mode;
    if (mode_str == "scraper" || mode_str == "s") {
        mode = OperationMode::SCRAPER;
    } else if (mode_str == "brute" || mode_str == "b") {
        mode = OperationMode::BRUTE_FORCE;
    } else if (mode_str == "hybrid" || mode_str == "h") {
        mode = OperationMode::HYBRID;
    } else {
        std::cerr << "Error: Invalid mode '" << mode_str << "'. Use: scraper, brute, or hybrid\n";
        return 1;
    }

    // Set up signal handling
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Get data set config with known ID ranges
    DataSetConfig config = get_data_set_config(data_set);

    // Override brute force range if specified on command line
    if (brute_start > 0) {
        config.first_file_id = brute_start;
    }
    if (brute_end > 0) {
        config.last_file_id = brute_end;
    }

    std::cout << "=== Epstein Files Grabber ===\n";
    std::cout << "Data Set: " << config.name << "\n";
    std::cout << "Mode: " << mode_str << "\n";
    std::cout << "Output: " << output_dir << "\n";
    std::cout << "Max Concurrent: " << max_concurrent << "\n";
    if (mode != OperationMode::SCRAPER && config.first_file_id > 0 && config.last_file_id > 0) {
        std::cout << "Brute Force Range: EFTA" << std::setw(8) << std::setfill('0')
                  << config.first_file_id << " - EFTA" << std::setw(8)
                  << std::setfill('0') << config.last_file_id << "\n";
    }
    std::cout << "\n";

    // Create download manager
    std::string db_path = "efgrabber.db";
    DownloadManager manager(db_path, output_dir);

    if (!manager.initialize()) {
        std::cerr << "Failed to initialize download manager\n";
        return 1;
    }

    manager.set_max_concurrent_downloads(max_concurrent);
    manager.set_retry_attempts(max_retries);
    if (!cookie_file.empty()) {
        manager.set_cookie_file(cookie_file);
        std::cout << "Using cookies from: " << cookie_file << "\n";
    }

    // Set up callbacks for console output
    DownloadCallbacks callbacks;

    // Thread-safe stats storage
    std::mutex stats_mutex;
    DownloadStats last_stats{};

    callbacks.on_stats_update = [&stats_mutex, &last_stats](const DownloadStats& stats) {
        std::lock_guard<std::mutex> lock(stats_mutex);
        last_stats = stats;
    };

    callbacks.on_complete = []() {
        std::cout << "\n[+] Download complete!\n";
    };

    callbacks.on_error = [](const std::string& error) {
        std::cerr << "[!] Error: " << error << "\n";
    };

    manager.set_callbacks(callbacks);

    // Start download
    manager.start(config, mode);

    // Main loop - print stats periodically
    auto last_print = std::chrono::steady_clock::now();

    while (manager.is_running() && !g_interrupted) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_print).count() >= 5) {
            DownloadStats stats = manager.get_stats();

            int64_t total = stats.files_completed + stats.files_failed +
                           stats.files_pending + stats.files_in_progress +
                           stats.files_not_found;
            double progress = total > 0 ? 100.0 * stats.files_completed / total : 0;

            std::cout << "\r[Stats] "
                      << "Progress: " << std::fixed << std::setprecision(1) << progress << "% | "
                      << "Completed: " << stats.files_completed << " | "
                      << "Failed: " << stats.files_failed << " | "
                      << "404: " << stats.files_not_found << " | "
                      << "Pending: " << stats.files_pending << " | "
                      << "Active: " << stats.files_in_progress << " | "
                      << "Speed: " << format_bytes(static_cast<int64_t>(stats.current_speed_bps)) << "/s"
                      << "          " << std::flush;

            last_print = now;
        }
    }

    if (g_interrupted) {
        manager.stop();
    }

    // Final stats
    DownloadStats final_stats = manager.get_stats();
    std::cout << "\n\n=== Final Statistics ===\n";
    std::cout << "Files completed: " << final_stats.files_completed << "\n";
    std::cout << "Files failed: " << final_stats.files_failed << "\n";
    std::cout << "Files not found (404): " << final_stats.files_not_found << "\n";
    std::cout << "Pages scraped: " << final_stats.pages_scraped << "/" << final_stats.total_pages << "\n";
    std::cout << "Total downloaded: " << format_bytes(final_stats.bytes_downloaded) << "\n";

    return 0;
}
