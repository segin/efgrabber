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
#include <memory>
#include <vector>
#include <string>
#include <atomic>

#include "efgrabber/download_manager.h"
#include "browser_widget.h"

namespace efgrabber {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

public slots:
    void onStartClicked();
    void onStopClicked();
    void onPauseClicked();
    void onBrowseClicked();
    void onCookieBrowseClicked();
    void onDataSetChanged(int index);
    void onModeChanged(int index);
    void onStatsTimer();

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

    // UI components
    QWidget* centralWidget_;
    QVBoxLayout* mainLayout_;
    QTabWidget* tabWidget_;
    QWidget* downloaderTab_;
    BrowserWidget* browserWidget_;

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
    QLabel* bytesLabel_;

    // Log view - using QPlainTextEdit for performance
    QGroupBox* logGroup_;
    QPlainTextEdit* logView_;

    // Control buttons
    QHBoxLayout* controlsLayout_;
    QPushButton* startButton_;
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
    std::atomic<bool> isRunning_;
    std::atomic<bool> isPaused_;

    // Browser scraping state
    std::atomic<bool> browserScrapingActive_;
    std::atomic<int> currentScrapePage_;
    int maxScrapePage_;
    std::atomic<int> pdfFoundCount_;
    QTimer* scrapeTimer_;
    QSet<QString> seenFileIds_;  // Track seen IDs to detect duplicate pages
    int consecutiveDuplicatePages_;  // Count pages with no new PDFs

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
