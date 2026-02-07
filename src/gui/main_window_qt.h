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
#include <QTextEdit>
#include <QGroupBox>
#include <QGridLayout>
#include <QTimer>
#include <QMutex>
#include <QLineEdit>
#include <QSpinBox>
#include <QTabWidget>
#include <memory>
#include <vector>
#include <string>

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

private:
    void setupUi();
    void startDownload(int dataSet, OperationMode mode);
    void startBrowserScraping(int dataSet);
    void stopDownload();
    void pauseDownload();
    void processBrowserHtml(const QString& html);
    QString formatBytes(int64_t bytes) const;
    QString formatSpeed(double bps) const;

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

    // Log view
    QGroupBox* logGroup_;
    QTextEdit* logView_;

    // Control buttons
    QHBoxLayout* controlsLayout_;
    QPushButton* startButton_;
    QPushButton* pauseButton_;
    QPushButton* stopButton_;

    // Download manager
    std::unique_ptr<DownloadManager> downloadManager_;

    // Timer for UI updates
    QTimer* statsTimer_;

    // State
    int selectedDataSet_;
    OperationMode selectedMode_;
    bool isRunning_;
    bool isPaused_;

    // Browser scraping state
    bool browserScrapingActive_;
    int currentScrapePage_;
    int maxScrapePage_;
    int pdfFoundCount_;
    QTimer* scrapeTimer_;

    // Thread-safe log queue
    QMutex logMutex_;
    std::vector<std::string> pendingLogs_;
};

} // namespace efgrabber

#endif // EFGRABBER_MAIN_WINDOW_QT_H
