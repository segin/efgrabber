/*
 * src/gui/main_window.cpp - Implementation of the GTK main window
 * Copyright (c) 2026 Kirn Gill II
 * SPDX-License-Identifier: MIT
 * See LICENSE file for full license text.
 */

#include "efgrabber/gui/main_window.h"
#include <sstream>
#include <iomanip>
#include <ctime>
#include <filesystem>

namespace fs = std::filesystem;

namespace efgrabber {

MainWindow::MainWindow(GtkApplication* app) {
    window_ = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window_), "Epstein Files Grabber");
    gtk_window_set_default_size(GTK_WINDOW(window_), 1000, 700);

    setup_ui();
}

MainWindow::~MainWindow() {
    stop_download();
}

void MainWindow::setup_ui() {
    main_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(main_box_, 12);
    gtk_widget_set_margin_end(main_box_, 12);
    gtk_widget_set_margin_top(main_box_, 12);
    gtk_widget_set_margin_bottom(main_box_, 12);

    setup_header_bar();
    setup_data_set_selector();
    setup_mode_selector();
    setup_progress_view();
    setup_log_view();
    setup_controls();

    gtk_window_set_child(GTK_WINDOW(window_), main_box_);
}

void MainWindow::setup_header_bar() {
    header_bar_ = gtk_header_bar_new();
    gtk_window_set_titlebar(GTK_WINDOW(window_), header_bar_);
}

void MainWindow::setup_data_set_selector() {
    GtkWidget* selector_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);

    GtkWidget* ds_label = gtk_label_new("Data Set:");
    gtk_box_append(GTK_BOX(selector_box), ds_label);

    data_set_list_ = gtk_string_list_new(nullptr);
    for (int i = MIN_DATA_SET; i <= MAX_DATA_SET; ++i) {
        std::string name = "Data Set " + std::to_string(i);
        gtk_string_list_append(data_set_list_, name.c_str());
    }

    data_set_dropdown_ = gtk_drop_down_new(G_LIST_MODEL(data_set_list_), nullptr);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(data_set_dropdown_), 10);  // Data Set 11 (0-indexed)
    g_signal_connect(data_set_dropdown_, "notify::selected",
                    G_CALLBACK(on_data_set_changed), this);
    gtk_box_append(GTK_BOX(selector_box), data_set_dropdown_);

    gtk_box_append(GTK_BOX(main_box_), selector_box);
}

void MainWindow::setup_mode_selector() {
    GtkWidget* mode_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);

    GtkWidget* mode_label = gtk_label_new("Mode:");
    gtk_box_append(GTK_BOX(mode_box), mode_label);

    mode_list_ = gtk_string_list_new(nullptr);
    gtk_string_list_append(mode_list_, "Scraper (parse index pages)");
    gtk_string_list_append(mode_list_, "Brute Force (try all IDs)");
    gtk_string_list_append(mode_list_, "Hybrid (both modes)");

    mode_dropdown_ = gtk_drop_down_new(G_LIST_MODEL(mode_list_), nullptr);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(mode_dropdown_), 0);
    g_signal_connect(mode_dropdown_, "notify::selected",
                    G_CALLBACK(on_mode_changed), this);
    gtk_box_append(GTK_BOX(mode_box), mode_dropdown_);

    gtk_box_append(GTK_BOX(main_box_), mode_box);
}

void MainWindow::setup_progress_view() {
    progress_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    // Overall progress
    GtkWidget* overall_frame = gtk_frame_new("Overall Progress");
    GtkWidget* overall_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(overall_box, 8);
    gtk_widget_set_margin_end(overall_box, 8);
    gtk_widget_set_margin_top(overall_box, 8);
    gtk_widget_set_margin_bottom(overall_box, 8);

    overall_progress_bar_ = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(overall_progress_bar_), TRUE);
    gtk_box_append(GTK_BOX(overall_box), overall_progress_bar_);

    overall_progress_label_ = gtk_label_new("Ready to start");
    gtk_label_set_xalign(GTK_LABEL(overall_progress_label_), 0);
    gtk_box_append(GTK_BOX(overall_box), overall_progress_label_);

    gtk_frame_set_child(GTK_FRAME(overall_frame), overall_box);
    gtk_box_append(GTK_BOX(progress_box_), overall_frame);

    // Stats grid
    stats_grid_ = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(stats_grid_), 24);
    gtk_grid_set_row_spacing(GTK_GRID(stats_grid_), 4);

    int row = 0;

    // Row 1: Completed, Failed, Pending
    gtk_grid_attach(GTK_GRID(stats_grid_), gtk_label_new("Completed:"), 0, row, 1, 1);
    files_completed_label_ = gtk_label_new("0");
    gtk_label_set_xalign(GTK_LABEL(files_completed_label_), 0);
    gtk_grid_attach(GTK_GRID(stats_grid_), files_completed_label_, 1, row, 1, 1);

    gtk_grid_attach(GTK_GRID(stats_grid_), gtk_label_new("Failed:"), 2, row, 1, 1);
    files_failed_label_ = gtk_label_new("0");
    gtk_label_set_xalign(GTK_LABEL(files_failed_label_), 0);
    gtk_grid_attach(GTK_GRID(stats_grid_), files_failed_label_, 3, row, 1, 1);

    gtk_grid_attach(GTK_GRID(stats_grid_), gtk_label_new("Pending:"), 4, row, 1, 1);
    files_pending_label_ = gtk_label_new("0");
    gtk_label_set_xalign(GTK_LABEL(files_pending_label_), 0);
    gtk_grid_attach(GTK_GRID(stats_grid_), files_pending_label_, 5, row, 1, 1);

    row++;

    // Row 2: Not Found, Active, Pages Scraped
    gtk_grid_attach(GTK_GRID(stats_grid_), gtk_label_new("Not Found:"), 0, row, 1, 1);
    files_not_found_label_ = gtk_label_new("0");
    gtk_label_set_xalign(GTK_LABEL(files_not_found_label_), 0);
    gtk_grid_attach(GTK_GRID(stats_grid_), files_not_found_label_, 1, row, 1, 1);

    gtk_grid_attach(GTK_GRID(stats_grid_), gtk_label_new("Active:"), 2, row, 1, 1);
    active_downloads_label_ = gtk_label_new("0");
    gtk_label_set_xalign(GTK_LABEL(active_downloads_label_), 0);
    gtk_grid_attach(GTK_GRID(stats_grid_), active_downloads_label_, 3, row, 1, 1);

    gtk_grid_attach(GTK_GRID(stats_grid_), gtk_label_new("Pages Scraped:"), 4, row, 1, 1);
    pages_scraped_label_ = gtk_label_new("0");
    gtk_label_set_xalign(GTK_LABEL(pages_scraped_label_), 0);
    gtk_grid_attach(GTK_GRID(stats_grid_), pages_scraped_label_, 5, row, 1, 1);

    row++;

    // Row 3: Speed, Downloaded
    gtk_grid_attach(GTK_GRID(stats_grid_), gtk_label_new("Speed:"), 0, row, 1, 1);
    speed_label_ = gtk_label_new("0 B/s");
    gtk_label_set_xalign(GTK_LABEL(speed_label_), 0);
    gtk_grid_attach(GTK_GRID(stats_grid_), speed_label_, 1, row, 1, 1);

    gtk_grid_attach(GTK_GRID(stats_grid_), gtk_label_new("Downloaded:"), 2, row, 1, 1);
    bytes_label_ = gtk_label_new("0 B");
    gtk_label_set_xalign(GTK_LABEL(bytes_label_), 0);
    gtk_grid_attach(GTK_GRID(stats_grid_), bytes_label_, 3, row, 1, 1);

    gtk_box_append(GTK_BOX(progress_box_), stats_grid_);

    // Scraper progress
    GtkWidget* scraper_frame = gtk_frame_new("Scraper Progress");
    GtkWidget* scraper_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(scraper_box, 8);
    gtk_widget_set_margin_end(scraper_box, 8);
    gtk_widget_set_margin_top(scraper_box, 8);
    gtk_widget_set_margin_bottom(scraper_box, 8);

    scraper_progress_bar_ = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(scraper_progress_bar_), TRUE);
    gtk_box_append(GTK_BOX(scraper_box), scraper_progress_bar_);

    scraper_progress_label_ = gtk_label_new("0 / 0 pages scraped");
    gtk_label_set_xalign(GTK_LABEL(scraper_progress_label_), 0);
    gtk_box_append(GTK_BOX(scraper_box), scraper_progress_label_);

    gtk_frame_set_child(GTK_FRAME(scraper_frame), scraper_box);
    gtk_box_append(GTK_BOX(progress_box_), scraper_frame);

    // Brute force progress
    GtkWidget* bf_frame = gtk_frame_new("Brute Force Progress");
    GtkWidget* bf_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(bf_box, 8);
    gtk_widget_set_margin_end(bf_box, 8);
    gtk_widget_set_margin_top(bf_box, 8);
    gtk_widget_set_margin_bottom(bf_box, 8);

    brute_force_progress_bar_ = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(brute_force_progress_bar_), TRUE);
    gtk_box_append(GTK_BOX(bf_box), brute_force_progress_bar_);

    brute_force_progress_label_ = gtk_label_new("EFTA00000000 - 0.00%");
    gtk_label_set_xalign(GTK_LABEL(brute_force_progress_label_), 0);
    gtk_box_append(GTK_BOX(bf_box), brute_force_progress_label_);

    gtk_frame_set_child(GTK_FRAME(bf_frame), bf_box);
    gtk_box_append(GTK_BOX(progress_box_), bf_frame);

    gtk_box_append(GTK_BOX(main_box_), progress_box_);
}

void MainWindow::setup_log_view() {
    GtkWidget* log_frame = gtk_frame_new("Log");

    log_scroll_ = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(log_scroll_),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(log_scroll_, TRUE);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(log_scroll_), 150);

    log_text_view_ = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log_text_view_), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(log_text_view_), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(log_text_view_), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(log_text_view_), GTK_WRAP_WORD_CHAR);

    log_buffer_ = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_text_view_));

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(log_scroll_), log_text_view_);
    gtk_frame_set_child(GTK_FRAME(log_frame), log_scroll_);

    gtk_box_append(GTK_BOX(main_box_), log_frame);
}

void MainWindow::setup_controls() {
    controls_box_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_halign(controls_box_, GTK_ALIGN_CENTER);

    start_button_ = gtk_button_new_with_label("Start");
    gtk_widget_add_css_class(start_button_, "suggested-action");
    g_signal_connect(start_button_, "clicked", G_CALLBACK(on_start_clicked), this);
    gtk_box_append(GTK_BOX(controls_box_), start_button_);

    pause_button_ = gtk_button_new_with_label("Pause");
    gtk_widget_set_sensitive(pause_button_, FALSE);
    g_signal_connect(pause_button_, "clicked", G_CALLBACK(on_pause_clicked), this);
    gtk_box_append(GTK_BOX(controls_box_), pause_button_);

    stop_button_ = gtk_button_new_with_label("Stop");
    gtk_widget_add_css_class(stop_button_, "destructive-action");
    gtk_widget_set_sensitive(stop_button_, FALSE);
    g_signal_connect(stop_button_, "clicked", G_CALLBACK(on_stop_clicked), this);
    gtk_box_append(GTK_BOX(controls_box_), stop_button_);

    gtk_box_append(GTK_BOX(main_box_), controls_box_);
}

void MainWindow::start_download(int data_set, OperationMode mode) {
    if (is_running_) return;

    // Create download directory
    std::string download_dir = "downloads";
    std::string db_path = "efgrabber.db";

    download_manager_ = std::make_unique<DownloadManager>(db_path, download_dir);

    if (!download_manager_->initialize()) {
        append_log("Failed to initialize download manager");
        return;
    }

    // Set callbacks
    DownloadCallbacks callbacks;

    callbacks.on_stats_update = [this](const DownloadStats& stats) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        pending_stats_ = stats;
        stats_pending_ = true;
        g_idle_add(on_stats_update, this);
    };

    callbacks.on_log_message = [this](const std::string& message) {
        std::lock_guard<std::mutex> lock(log_mutex_);
        pending_logs_.push_back(message);
        g_idle_add(on_log_update, this);
    };

    callbacks.on_file_status_change = [this](const std::string& file_id, DownloadStatus status) {
        // Could add per-file updates to GUI if needed
    };

    callbacks.on_page_scraped = [this](int page, int count) {
        std::lock_guard<std::mutex> lock(log_mutex_);
        pending_logs_.push_back("Scraped page " + std::to_string(page) +
                               " (" + std::to_string(count) + " PDFs)");
        g_idle_add(on_log_update, this);
    };

    callbacks.on_complete = [this]() {
        std::lock_guard<std::mutex> lock(log_mutex_);
        pending_logs_.push_back("Download complete!");
        g_idle_add(on_log_update, this);
    };

    callbacks.on_error = [this](const std::string& error) {
        std::lock_guard<std::mutex> lock(log_mutex_);
        pending_logs_.push_back("ERROR: " + error);
        g_idle_add(on_log_update, this);
    };

    download_manager_->set_callbacks(callbacks);

    // Get data set config
    DataSetConfig config;
    if (data_set == 11) {
        config = get_data_set_11_config();
    } else {
        config = make_data_set_config(data_set);
    }

    download_manager_->start(config, mode);

    is_running_ = true;
    is_paused_ = false;

    gtk_widget_set_sensitive(start_button_, FALSE);
    gtk_widget_set_sensitive(stop_button_, TRUE);
    gtk_widget_set_sensitive(pause_button_, TRUE);
    gtk_widget_set_sensitive(data_set_dropdown_, FALSE);
    gtk_widget_set_sensitive(mode_dropdown_, FALSE);

    append_log("Started downloading " + config.name);
}

void MainWindow::stop_download() {
    if (!is_running_ || !download_manager_) return;

    download_manager_->stop();
    download_manager_.reset();

    is_running_ = false;
    is_paused_ = false;

    gtk_widget_set_sensitive(start_button_, TRUE);
    gtk_widget_set_sensitive(stop_button_, FALSE);
    gtk_widget_set_sensitive(pause_button_, FALSE);
    gtk_widget_set_sensitive(data_set_dropdown_, TRUE);
    gtk_widget_set_sensitive(mode_dropdown_, TRUE);

    gtk_button_set_label(GTK_BUTTON(pause_button_), "Pause");

    append_log("Download stopped");
}

void MainWindow::pause_download() {
    if (!is_running_ || !download_manager_) return;

    if (is_paused_) {
        download_manager_->resume();
        is_paused_ = false;
        gtk_button_set_label(GTK_BUTTON(pause_button_), "Pause");
        append_log("Download resumed");
    } else {
        download_manager_->pause();
        is_paused_ = true;
        gtk_button_set_label(GTK_BUTTON(pause_button_), "Resume");
        append_log("Download paused");
    }
}

void MainWindow::update_stats(const DownloadStats& stats) {
    // Overall progress
    int64_t total = stats.files_completed + stats.files_failed + stats.files_pending +
                   stats.files_in_progress + stats.files_not_found;
    double progress = total > 0 ? static_cast<double>(stats.files_completed) / total : 0;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(overall_progress_bar_), progress);

    std::ostringstream overall_text;
    overall_text << stats.files_completed << " / " << total << " files ("
                 << std::fixed << std::setprecision(1) << (progress * 100) << "%)";
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(overall_progress_bar_), overall_text.str().c_str());

    // Stats labels
    gtk_label_set_text(GTK_LABEL(files_completed_label_), std::to_string(stats.files_completed).c_str());
    gtk_label_set_text(GTK_LABEL(files_failed_label_), std::to_string(stats.files_failed).c_str());
    gtk_label_set_text(GTK_LABEL(files_pending_label_), std::to_string(stats.files_pending).c_str());
    gtk_label_set_text(GTK_LABEL(files_not_found_label_), std::to_string(stats.files_not_found).c_str());
    gtk_label_set_text(GTK_LABEL(active_downloads_label_), std::to_string(stats.files_in_progress).c_str());
    gtk_label_set_text(GTK_LABEL(speed_label_), format_speed(stats.current_speed_bps).c_str());
    gtk_label_set_text(GTK_LABEL(bytes_label_), format_bytes(stats.bytes_downloaded).c_str());

    // Scraper progress
    if (stats.total_pages > 0) {
        double scraper_progress = static_cast<double>(stats.pages_scraped) / stats.total_pages;
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(scraper_progress_bar_), scraper_progress);

        std::ostringstream scraper_text;
        scraper_text << stats.pages_scraped << " / " << stats.total_pages << " pages";
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(scraper_progress_bar_), scraper_text.str().c_str());

        std::ostringstream scraper_label;
        scraper_label << stats.pages_scraped << " / " << stats.total_pages
                     << " pages scraped (" << stats.total_files_found << " PDFs found)";
        gtk_label_set_text(GTK_LABEL(scraper_progress_label_), scraper_label.str().c_str());
        gtk_label_set_text(GTK_LABEL(pages_scraped_label_), std::to_string(stats.pages_scraped).c_str());
    }

    // Brute force progress
    if (stats.brute_force_end > stats.brute_force_start) {
        uint64_t range = stats.brute_force_end - stats.brute_force_start;
        uint64_t done = stats.brute_force_current - stats.brute_force_start;
        double bf_progress = static_cast<double>(done) / range;
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(brute_force_progress_bar_), bf_progress);

        std::ostringstream bf_text;
        bf_text << std::fixed << std::setprecision(2) << (bf_progress * 100) << "%";
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(brute_force_progress_bar_), bf_text.str().c_str());

        std::ostringstream bf_label;
        bf_label << "EFTA" << std::setw(8) << std::setfill('0') << stats.brute_force_current
                << " - " << std::fixed << std::setprecision(2) << (bf_progress * 100) << "%"
                << " (" << done << " / " << range << ")";
        gtk_label_set_text(GTK_LABEL(brute_force_progress_label_), bf_label.str().c_str());
    }
}

void MainWindow::append_log(const std::string& message) {
    // Get timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    localtime_r(&time_t, &tm_buf);

    std::ostringstream log_line;
    log_line << "[" << std::put_time(&tm_buf, "%H:%M:%S") << "] " << message << "\n";

    GtkTextIter end;
    gtk_text_buffer_get_end_iter(log_buffer_, &end);
    gtk_text_buffer_insert(log_buffer_, &end, log_line.str().c_str(), -1);

    // Scroll to bottom
    gtk_text_buffer_get_end_iter(log_buffer_, &end);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(log_text_view_), &end, 0.0, FALSE, 0.0, 0.0);
}

// Static callbacks
void MainWindow::on_start_clicked(GtkButton* /*button*/, gpointer user_data) {
    auto* self = static_cast<MainWindow*>(user_data);
    self->start_download(self->selected_data_set_, self->selected_mode_);
}

void MainWindow::on_stop_clicked(GtkButton* /*button*/, gpointer user_data) {
    auto* self = static_cast<MainWindow*>(user_data);
    self->stop_download();
}

void MainWindow::on_pause_clicked(GtkButton* /*button*/, gpointer user_data) {
    auto* self = static_cast<MainWindow*>(user_data);
    self->pause_download();
}

void MainWindow::on_data_set_changed(GtkDropDown* dropdown, GParamSpec* /*pspec*/, gpointer user_data) {
    auto* self = static_cast<MainWindow*>(user_data);
    guint selected = gtk_drop_down_get_selected(dropdown);
    self->selected_data_set_ = MIN_DATA_SET + static_cast<int>(selected);
}

void MainWindow::on_mode_changed(GtkDropDown* dropdown, GParamSpec* /*pspec*/, gpointer user_data) {
    auto* self = static_cast<MainWindow*>(user_data);
    guint selected = gtk_drop_down_get_selected(dropdown);
    switch (selected) {
        case 0: self->selected_mode_ = OperationMode::SCRAPER; break;
        case 1: self->selected_mode_ = OperationMode::BRUTE_FORCE; break;
        case 2: self->selected_mode_ = OperationMode::HYBRID; break;
    }
}

gboolean MainWindow::on_stats_update(gpointer user_data) {
    auto* self = static_cast<MainWindow*>(user_data);
    if (self->stats_pending_) {
        DownloadStats stats;
        {
            std::lock_guard<std::mutex> lock(self->stats_mutex_);
            stats = self->pending_stats_;
            self->stats_pending_ = false;
        }
        self->update_stats(stats);
    }
    return G_SOURCE_REMOVE;
}

gboolean MainWindow::on_log_update(gpointer user_data) {
    auto* self = static_cast<MainWindow*>(user_data);
    std::vector<std::string> logs;
    {
        std::lock_guard<std::mutex> lock(self->log_mutex_);
        logs = std::move(self->pending_logs_);
        self->pending_logs_.clear();
    }
    for (const auto& log : logs) {
        self->append_log(log);
    }
    return G_SOURCE_REMOVE;
}

// Helpers
std::string MainWindow::format_bytes(int64_t bytes) const {
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

std::string MainWindow::format_speed(double bps) const {
    return format_bytes(static_cast<int64_t>(bps)) + "/s";
}

std::string MainWindow::format_time(double seconds) const {
    int hours = static_cast<int>(seconds) / 3600;
    int minutes = (static_cast<int>(seconds) % 3600) / 60;
    int secs = static_cast<int>(seconds) % 60;

    std::ostringstream oss;
    if (hours > 0) {
        oss << hours << "h " << minutes << "m " << secs << "s";
    } else if (minutes > 0) {
        oss << minutes << "m " << secs << "s";
    } else {
        oss << secs << "s";
    }
    return oss.str();
}

// Application entry point
static void on_activate(GtkApplication* app, gpointer /*user_data*/) {
    auto* window = new MainWindow(app);
    gtk_window_present(GTK_WINDOW(window->get_window()));
}

int run_gui(int argc, char** argv) {
    GtkApplication* app = gtk_application_new("com.efgrabber.app", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), nullptr);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}

} // namespace efgrabber
