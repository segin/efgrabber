/*
 * src/gui/main_window_qt.h - Main window class for the Qt GUI application
 * Copyright (c) 2026 Kirn Gill II
 * SPDX-License-Identifier: MIT
 * See LICENSE file for full license text.
 */

#ifndef EFGRABBER_MAIN_WINDOW_QT_H
#define EFGRABBER_MAIN_WINDOW_QT_H

#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QGroupBox>
#include <QGridLayout>
#include <QTimer>
#include <QMutex>
#include <QLineEdit>
#include <QSpinBox>
#include <QTabWidget>
#include <QElapsedTimer>
#include <QCheckBox>
#include <QCloseEvent>
#include <memory>
#include <vector>
#include <string>
#include <atomic>

#include "efgrabber/download_manager.h"
#include "browser_widget.h"
#include "scraper_pool.h"

namespace efgrabber {

// Log verbosity levels
enum class LogLevel {
    QUIET,    // Errors and completion only
    NORMAL,   // + Page scrapes, summaries
    VERBOSE,  // + Individual file downloads
    DEBUG     // Everything
};

// Log channels (can be filtered independently)
enum class LogChannel {
    SYSTEM,    // App lifecycle, errors
    SCRAPER,   // Page scraping activity
    DOWNLOAD,  // File download activity
    DEBUG      // Internal debugging
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

public slots:
    void onStartClicked();
    void onResumeClicked();
    void onRetryFailedClicked();
    void onRedownloadAllClicked();
    void onClearDataSetClicked();
    void onStopClicked();
    void onPauseClicked();
    void onBrowseClicked();
    void onCookieBrowseClicked();
    void onDataSetChanged(int index);
    void onModeChanged(int index);
    void onStatsTimer();
    void onThreadCountChanged(int value);

signals:
    void logMessageReceived(const QString& message);
    void statsReceived(const DownloadStats& stats);
    void pageScraped(int page, int count);
    void downloadComplete();
    void errorOccurred(const QString& error);

private slots:
    void appendLog(const QString& message);
    void updateStats(const DownloadStats& stats);
    void handlePageScraped(int page, int count);
    void handleDownloadComplete();
    void handleError(const QString& error);
    void onBrowserPageReady(const QString& url, const QString& html);
    void onScraperPageReady(int pageNumber, const QString& html);
    void onScrapingComplete();
    void scrapeNextPage();
    void flushPendingLogs();

private:
    void setupUi();
    void startDownload(int dataSet, OperationMode mode);
    void startBrowserScraping(int dataSet);
    void stopDownload();
    void pauseDownload();
    void processBrowserHtml(const QString& html);
    QString formatBytes(int64_t bytes) const;
    QString formatSpeed(double bps) const;
    void updateProgressBar(QProgressBar* bar, int value);
    void updateLabel(QLabel* label, const QString& text);

    // Settings persistence
    void loadSettings();
    void saveSettings();

    // Leveled logging with channels
    void log(LogLevel level, LogChannel channel, const QString& message);
    void logQuiet(LogChannel channel, const QString& message);    // Errors and completion
    void logNormal(LogChannel channel, const QString& message);   // Page scrapes, summaries
    void logVerbose(LogChannel channel, const QString& message);  // Individual files
    void logDebug(LogChannel channel, const QString& message);    // Everything
    bool shouldLog(LogLevel level, LogChannel channel) const;

    // UI components
    QWidget* centralWidget_;
    QVBoxLayout* mainLayout_;
    QTabWidget* tabWidget_;
    QWidget* downloaderTab_;
    BrowserWidget* browserWidget_;
    ScraperPool* scraperPool_;

    // Data set selector
    QComboBox* dataSetCombo_;

    // Mode selector
    QComboBox* modeCombo_;

    // Download folder
    QLineEdit* downloadPathEdit_;
    QPushButton* browseButton_;

    // Cookie file
    QLineEdit* cookieFileEdit_;
    QPushButton* cookieBrowseButton_;

    // Brute force range
    QSpinBox* startIdSpin_;
    QSpinBox* endIdSpin_;

    // Download thread count control
    QSpinBox* threadCountSpin_;
    QSpinBox* scraperTabCountSpin_;

    // Force parallel scraping
    QCheckBox* forceParallelCheck_;
    QSpinBox* forceMaxPageSpin_;

    QLabel* activeDownloadsLabel_;

    // Download options
    QCheckBox* overwriteExistingCheck_;

    // Log verbosity control
    QComboBox* logVerbosityCombo_;

    // Log channel filters (checkboxes)
    QCheckBox* logSystemCheck_;
    QCheckBox* logScraperCheck_;
    QCheckBox* logDownloadCheck_;
    QCheckBox* logDebugCheck_;

    // Progress bars
    QGroupBox* overallGroup_;
    QProgressBar* overallProgress_;
    QLabel* overallLabel_;

    QGroupBox* scraperGroup_;
    QProgressBar* scraperProgress_;
    QLabel* scraperLabel_;

    QGroupBox* bruteForceGroup_;
    QProgressBar* bruteForceProgress_;
    QLabel* bruteForceLabel_;

    // Stats display
    QGroupBox* statsGroup_;
    QGridLayout* statsGrid_;
    QLabel* completedLabel_;
    QLabel* failedLabel_;
    QLabel* pendingLabel_;
    QLabel* notFoundLabel_;
    QLabel* activeLabel_;
    QLabel* pagesLabel_;
    QLabel* speedLabel_;
    QLabel* wireSpeedLabel_;
    QLabel* bytesLabel_;

    // Log view - using QPlainTextEdit for performance
    QGroupBox* logGroup_;
    QPlainTextEdit* logView_;

    // Control buttons
    QHBoxLayout* controlsLayout_;
    QPushButton* startButton_;
    QPushButton* resumeButton_;
    QPushButton* retryFailedButton_;
    QPushButton* redownloadAllButton_;
    QPushButton* clearDataSetButton_;
    QPushButton* pauseButton_;
    QPushButton* stopButton_;

    // Download manager
    std::unique_ptr<DownloadManager> downloadManager_;

    // Timers
    QTimer* statsTimer_;
    QTimer* logFlushTimer_;

    // State
    int selectedDataSet_;
    OperationMode selectedMode_;
    LogLevel logLevel_;
    std::atomic<bool> isRunning_;
    std::atomic<bool> isPaused_;

    // Browser scraping state
    std::atomic<bool> browserScrapingActive_;
    std::atomic<int> currentScrapePage_;
    int maxScrapePage_;
    int detectedLastPage_;  // Actual last page detected from pagination
    std::atomic<int> pdfFoundCount_;
    QTimer* scrapeTimer_;
    QSet<QString> seenFileIds_;
    bool detectingMaxPage_;  // True while detecting max page number
    bool verifyingFirstPage_;  // True when verifying anti-bot with page 0

    // Cached label values for change detection
    QString lastOverallLabel_;
    QString lastScraperLabel_;
    QString lastBruteForceLabel_;
    int lastOverallProgress_ = -1;
    int lastScraperProgress_ = -1;
    int lastBruteForceProgress_ = -1;

    // Log batching
    QMutex logMutex_;
    QStringList pendingLogs_;
    static constexpr int MAX_LOG_LINES = 500;
    static constexpr int LOG_FLUSH_INTERVAL_MS = 100;
};

} // namespace efgrabber

#endif // EFGRABBER_MAIN_WINDOW_QT_H
