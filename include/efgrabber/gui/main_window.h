#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <atomic>
#include "efgrabber/download_manager.h"

namespace efgrabber {

class MainWindow {
public:
    MainWindow(GtkApplication* app);
    ~MainWindow();

    // Non-copyable
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    GtkWidget* get_window() const { return window_; }

    // Start the download manager
    void start_download(int data_set, OperationMode mode);
    void stop_download();
    void pause_download();
    void resume_download();

private:
    // UI setup
    void setup_ui();
    void setup_header_bar();
    void setup_data_set_selector();
    void setup_mode_selector();
    void setup_progress_view();
    void setup_log_view();
    void setup_controls();

    // UI update methods (called from main thread)
    void update_stats(const DownloadStats& stats);
    void append_log(const std::string& message);
    void on_file_status_change(const std::string& file_id, DownloadStatus status);
    void on_page_scraped(int page, int count);
    void on_download_complete();
    void on_error(const std::string& error);

    // Static callback wrappers for GTK
    static void on_start_clicked(GtkButton* button, gpointer user_data);
    static void on_stop_clicked(GtkButton* button, gpointer user_data);
    static void on_pause_clicked(GtkButton* button, gpointer user_data);
    static void on_data_set_changed(GtkDropDown* dropdown, GParamSpec* pspec, gpointer user_data);
    static void on_mode_changed(GtkDropDown* dropdown, GParamSpec* pspec, gpointer user_data);
    static gboolean on_stats_update(gpointer user_data);
    static gboolean on_log_update(gpointer user_data);

    // Helpers
    std::string format_bytes(int64_t bytes) const;
    std::string format_speed(double bps) const;
    std::string format_time(double seconds) const;

    // GTK widgets
    GtkWidget* window_ = nullptr;
    GtkWidget* main_box_ = nullptr;
    GtkWidget* header_bar_ = nullptr;

    // Data set selector
    GtkWidget* data_set_dropdown_ = nullptr;
    GtkStringList* data_set_list_ = nullptr;

    // Mode selector
    GtkWidget* mode_dropdown_ = nullptr;
    GtkStringList* mode_list_ = nullptr;

    // Progress display
    GtkWidget* progress_box_ = nullptr;
    GtkWidget* overall_progress_bar_ = nullptr;
    GtkWidget* overall_progress_label_ = nullptr;
    GtkWidget* scraper_progress_bar_ = nullptr;
    GtkWidget* scraper_progress_label_ = nullptr;
    GtkWidget* brute_force_progress_bar_ = nullptr;
    GtkWidget* brute_force_progress_label_ = nullptr;

    // Stats display
    GtkWidget* stats_grid_ = nullptr;
    GtkWidget* files_completed_label_ = nullptr;
    GtkWidget* files_failed_label_ = nullptr;
    GtkWidget* files_pending_label_ = nullptr;
    GtkWidget* files_not_found_label_ = nullptr;
    GtkWidget* speed_label_ = nullptr;
    GtkWidget* bytes_label_ = nullptr;
    GtkWidget* active_downloads_label_ = nullptr;
    GtkWidget* pages_scraped_label_ = nullptr;

    // Log view
    GtkWidget* log_scroll_ = nullptr;
    GtkWidget* log_text_view_ = nullptr;
    GtkTextBuffer* log_buffer_ = nullptr;

    // Control buttons
    GtkWidget* controls_box_ = nullptr;
    GtkWidget* start_button_ = nullptr;
    GtkWidget* stop_button_ = nullptr;
    GtkWidget* pause_button_ = nullptr;

    // Download manager
    std::unique_ptr<DownloadManager> download_manager_;

    // Thread-safe update queue
    std::mutex log_mutex_;
    std::vector<std::string> pending_logs_;
    std::atomic<bool> stats_pending_{false};
    DownloadStats pending_stats_;
    std::mutex stats_mutex_;

    // State
    int selected_data_set_ = 11;
    OperationMode selected_mode_ = OperationMode::SCRAPER;
    bool is_running_ = false;
    bool is_paused_ = false;
};

// Application entry point
int run_gui(int argc, char** argv);

} // namespace efgrabber
