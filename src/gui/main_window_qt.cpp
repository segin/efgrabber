/*
 * src/gui/main_window_qt.cpp - Implementation of the main window class
 * Copyright (c) 2026 Kirn Gill II
 * SPDX-License-Identifier: MIT
 * See LICENSE file for full license text.
 */

#include "main_window_qt.h"
#include <QDateTime>
#include <QScrollBar>
#include <QApplication>
#include <QFileDialog>
#include <QStatusBar>
#include <QRegularExpression>
#include <QSet>
#include <QTextCursor>
#include <QtConcurrent>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <iostream>
#include <sstream>
#include <iomanip>

namespace efgrabber {

// Settings file location
static QString getSettingsPath() {
    QString configDir = QDir::homePath() + "/.config/efgrabber";
    QDir().mkpath(configDir);
    return configDir + "/settings.conf";
}

void MainWindow::loadSettings() {
    QSettings settings(getSettingsPath(), QSettings::IniFormat);

    // Restore window geometry
    if (settings.contains("window/geometry")) {
        restoreGeometry(settings.value("window/geometry").toByteArray());
    }

    // Restore data set selection
    int dataSet = settings.value("download/dataSet", 11).toInt();
    int dataSetIndex = dataSet - 1;  // Data sets are 1-12, combo indices are 0-11
    if (dataSetIndex >= 0 && dataSetIndex < dataSetCombo_->count()) {
        dataSetCombo_->setCurrentIndex(dataSetIndex);
        selectedDataSet_ = dataSet;
    }

    // Restore mode selection
    int modeIndex = settings.value("download/modeIndex", 0).toInt();
    if (modeIndex >= 0 && modeIndex < modeCombo_->count()) {
        modeCombo_->setCurrentIndex(modeIndex);
    }

    // Restore download path
    QString downloadPath = settings.value("download/path", "downloads").toString();
    downloadPathEdit_->setText(downloadPath);

    // Restore cookie file path
    QString cookiePath = settings.value("download/cookieFile", "").toString();
    cookieFileEdit_->setText(cookiePath);

    // Restore thread count
    int threadCount = settings.value("download/threadCount", 50).toInt();
    threadCountSpin_->setValue(threadCount);

    // Restore overwrite existing setting
    bool overwriteExisting = settings.value("download/overwriteExisting", false).toBool();
    overwriteExistingCheck_->setChecked(overwriteExisting);

    // Restore log verbosity
    int verbosity = settings.value("log/verbosity", static_cast<int>(LogLevel::NORMAL)).toInt();
    logVerbosityCombo_->setCurrentIndex(verbosity);
    logLevel_ = static_cast<LogLevel>(verbosity);

    // Restore log channel filters
    logSystemCheck_->setChecked(settings.value("log/showSystem", true).toBool());
    logScraperCheck_->setChecked(settings.value("log/showScraper", true).toBool());
    logDownloadCheck_->setChecked(settings.value("log/showDownload", true).toBool());
    logDebugCheck_->setChecked(settings.value("log/showDebug", false).toBool());

    // Restore force parallel settings
    forceParallelCheck_->setChecked(settings.value("scraper/forceParallel", false).toBool());
    forceMaxPageSpin_->setValue(settings.value("scraper/forceMaxPage", 500).toInt());
}

void MainWindow::saveSettings() {
    QSettings settings(getSettingsPath(), QSettings::IniFormat);

    // Save window geometry
    settings.setValue("window/geometry", saveGeometry());

    // Save data set selection
    settings.setValue("download/dataSet", selectedDataSet_);

    // Save mode selection
    settings.setValue("download/modeIndex", modeCombo_->currentIndex());

    // Save download path
    settings.setValue("download/path", downloadPathEdit_->text());

    // Save cookie file path
    settings.setValue("download/cookieFile", cookieFileEdit_->text());

    // Save thread count
    settings.setValue("download/threadCount", threadCountSpin_->value());

    // Save overwrite existing setting
    settings.setValue("download/overwriteExisting", overwriteExistingCheck_->isChecked());

    // Save log verbosity
    settings.setValue("log/verbosity", static_cast<int>(logLevel_));

    // Save log channel filters
    settings.setValue("log/showSystem", logSystemCheck_->isChecked());
    settings.setValue("log/showScraper", logScraperCheck_->isChecked());
    settings.setValue("log/showDownload", logDownloadCheck_->isChecked());
    settings.setValue("log/showDebug", logDebugCheck_->isChecked());

    // Save force parallel settings
    settings.setValue("scraper/forceParallel", forceParallelCheck_->isChecked());
    settings.setValue("scraper/forceMaxPage", forceMaxPageSpin_->value());

    settings.sync();
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , selectedDataSet_(11)
    , selectedMode_(OperationMode::SCRAPER)
    , logLevel_(LogLevel::NORMAL)
    , isRunning_(false)
    , isPaused_(false)
    , browserScrapingActive_(false)
    , currentScrapePage_(0)
    , maxScrapePage_(20000)
    , pdfFoundCount_(0)
    , scrapeTimer_(nullptr)
{
    // Register DownloadStats for Qt signal/slot system
    qRegisterMetaType<DownloadStats>("DownloadStats");

    setWindowTitle("Epstein Files Grabber");
    resize(1000, 700);

    setupUi();

    // Load saved settings
    loadSettings();

    // Connect signals with queued connections for thread safety
    connect(this, &MainWindow::logMessageReceived, this, &MainWindow::appendLog, Qt::QueuedConnection);
    connect(this, &MainWindow::statsReceived, this, &MainWindow::updateStats, Qt::QueuedConnection);
    connect(this, &MainWindow::pageScraped, this, &MainWindow::handlePageScraped, Qt::QueuedConnection);
    connect(this, &MainWindow::downloadComplete, this, &MainWindow::handleDownloadComplete, Qt::QueuedConnection);
    connect(this, &MainWindow::errorOccurred, this, &MainWindow::handleError, Qt::QueuedConnection);

    // Stats timer - update every 2 seconds to reduce CPU usage
    statsTimer_ = new QTimer(this);
    statsTimer_->setTimerType(Qt::CoarseTimer);
    connect(statsTimer_, &QTimer::timeout, this, &MainWindow::onStatsTimer);

    // Log flush timer for batched log updates
    logFlushTimer_ = new QTimer(this);
    logFlushTimer_->setTimerType(Qt::CoarseTimer);
    connect(logFlushTimer_, &QTimer::timeout, this, &MainWindow::flushPendingLogs);
    logFlushTimer_->start(LOG_FLUSH_INTERVAL_MS);
}

MainWindow::~MainWindow() {
    stopDownload();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    saveSettings();
    stopDownload();
    event->accept();
}

void MainWindow::setupUi() {
    centralWidget_ = new QWidget(this);
    setCentralWidget(centralWidget_);

    mainLayout_ = new QVBoxLayout(centralWidget_);
    mainLayout_->setContentsMargins(6, 6, 6, 6);
    mainLayout_->setSpacing(6);

    // Create tab widget
    tabWidget_ = new QTabWidget();
    mainLayout_->addWidget(tabWidget_);

    // Downloader tab
    downloaderTab_ = new QWidget();
    QVBoxLayout* downloaderLayout = new QVBoxLayout(downloaderTab_);
    downloaderLayout->setContentsMargins(6, 6, 6, 6);
    downloaderLayout->setSpacing(8);

    // Data set selector
    QHBoxLayout* dataSetLayout = new QHBoxLayout();
    dataSetLayout->addWidget(new QLabel("Data Set:"));
    dataSetCombo_ = new QComboBox();
    for (int i = MIN_DATA_SET; i <= MAX_DATA_SET; ++i) {
        dataSetCombo_->addItem(QString("Data Set %1").arg(i), i);
    }
    dataSetCombo_->setCurrentIndex(10);  // Data Set 11
    connect(dataSetCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onDataSetChanged);
    dataSetLayout->addWidget(dataSetCombo_);
    dataSetLayout->addStretch();
    downloaderLayout->addLayout(dataSetLayout);

    // Mode selector
    QHBoxLayout* modeLayout = new QHBoxLayout();
    modeLayout->addWidget(new QLabel("Mode:"));
    modeCombo_ = new QComboBox();
    modeCombo_->addItem("Scraper (parse index pages)", static_cast<int>(OperationMode::SCRAPER));
    modeCombo_->addItem("Brute Force (try all IDs)", static_cast<int>(OperationMode::BRUTE_FORCE));
    modeCombo_->addItem("Hybrid (both modes)", static_cast<int>(OperationMode::HYBRID));
    connect(modeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onModeChanged);
    modeLayout->addWidget(modeCombo_);
    modeLayout->addStretch();
    downloaderLayout->addLayout(modeLayout);

    // Download folder selector
    QHBoxLayout* folderLayout = new QHBoxLayout();
    folderLayout->addWidget(new QLabel("Download Folder:"));
    downloadPathEdit_ = new QLineEdit("downloads");
    folderLayout->addWidget(downloadPathEdit_);
    browseButton_ = new QPushButton("Browse...");
    connect(browseButton_, &QPushButton::clicked, this, &MainWindow::onBrowseClicked);
    folderLayout->addWidget(browseButton_);
    downloaderLayout->addLayout(folderLayout);

    // Cookie file selector (fallback - browser cookies are preferred)
    QHBoxLayout* cookieLayout = new QHBoxLayout();
    cookieLayout->addWidget(new QLabel("Cookie File (fallback):"));
    cookieFileEdit_ = new QLineEdit();
    cookieFileEdit_->setPlaceholderText("Browser cookies are used automatically");
    cookieLayout->addWidget(cookieFileEdit_);
    cookieBrowseButton_ = new QPushButton("Browse...");
    connect(cookieBrowseButton_, &QPushButton::clicked, this, &MainWindow::onCookieBrowseClicked);
    cookieLayout->addWidget(cookieBrowseButton_);
    downloaderLayout->addLayout(cookieLayout);

    // Brute force ID range
    QHBoxLayout* rangeLayout = new QHBoxLayout();
    rangeLayout->addWidget(new QLabel("Brute Force Range:"));
    rangeLayout->addWidget(new QLabel("Start ID:"));
    startIdSpin_ = new QSpinBox();
    startIdSpin_->setRange(0, 99999999);
    startIdSpin_->setValue(2205655);
    rangeLayout->addWidget(startIdSpin_);
    rangeLayout->addWidget(new QLabel("End ID:"));
    endIdSpin_ = new QSpinBox();
    endIdSpin_->setRange(0, 99999999);
    endIdSpin_->setValue(2730262);
    rangeLayout->addWidget(endIdSpin_);
    rangeLayout->addStretch();
    downloaderLayout->addLayout(rangeLayout);

    // Download thread count control
    QHBoxLayout* threadLayout = new QHBoxLayout();
    threadLayout->addWidget(new QLabel("Download Threads:"));
    threadCountSpin_ = new QSpinBox();
    threadCountSpin_->setRange(1, 500);
    threadCountSpin_->setValue(50);  // Default 50 threads
    threadCountSpin_->setToolTip("Number of concurrent download threads (adjustable at runtime)");
    connect(threadCountSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::onThreadCountChanged);
    threadLayout->addWidget(threadCountSpin_);
    activeDownloadsLabel_ = new QLabel("");
    threadLayout->addWidget(activeDownloadsLabel_);

    threadLayout->addSpacing(20);
    threadLayout->addWidget(new QLabel("Scraper Tabs:"));
    scraperTabCountSpin_ = new QSpinBox();
    scraperTabCountSpin_->setRange(1, 10);
    scraperTabCountSpin_->setValue(1);  // Default 1 tab
    scraperTabCountSpin_->setToolTip("Number of concurrent browser tabs for scraping (1-10)");
    threadLayout->addWidget(scraperTabCountSpin_);

    threadLayout->addSpacing(20);
    forceParallelCheck_ = new QCheckBox("Force Parallel Max:");
    forceParallelCheck_->setToolTip("If page count detection fails, use the specified max page for parallel scraping");
    threadLayout->addWidget(forceParallelCheck_);

    forceMaxPageSpin_ = new QSpinBox();
    forceMaxPageSpin_->setRange(1, 1000000);
    forceMaxPageSpin_->setValue(500);
    threadLayout->addWidget(forceMaxPageSpin_);

    threadLayout->addStretch();
    downloaderLayout->addLayout(threadLayout);

    // Download options row
    QHBoxLayout* optionsLayout = new QHBoxLayout();
    overwriteExistingCheck_ = new QCheckBox("Overwrite existing files");
    overwriteExistingCheck_->setToolTip("Re-download files that already exist on disk");
    optionsLayout->addWidget(overwriteExistingCheck_);
    optionsLayout->addStretch();
    downloaderLayout->addLayout(optionsLayout);

    // Overall progress
    overallGroup_ = new QGroupBox("Overall Progress");
    QVBoxLayout* overallLayout = new QVBoxLayout(overallGroup_);
    overallProgress_ = new QProgressBar();
    overallProgress_->setRange(0, 100);
    overallProgress_->setValue(0);
    overallProgress_->setTextVisible(true);
    overallLayout->addWidget(overallProgress_);
    overallLabel_ = new QLabel("Ready to start");
    overallLayout->addWidget(overallLabel_);
    downloaderLayout->addWidget(overallGroup_);

    // Stats grid - compact layout
    statsGroup_ = new QGroupBox("Statistics");
    statsGrid_ = new QGridLayout(statsGroup_);

    int row = 0;
    statsGrid_->addWidget(new QLabel("Completed:"), row, 0);
    completedLabel_ = new QLabel("0");
    statsGrid_->addWidget(completedLabel_, row, 1);
    statsGrid_->addWidget(new QLabel("Failed:"), row, 2);
    failedLabel_ = new QLabel("0");
    statsGrid_->addWidget(failedLabel_, row, 3);
    statsGrid_->addWidget(new QLabel("Pending:"), row, 4);
    pendingLabel_ = new QLabel("0");
    statsGrid_->addWidget(pendingLabel_, row, 5);

    row++;
    statsGrid_->addWidget(new QLabel("404:"), row, 0);
    notFoundLabel_ = new QLabel("0");
    statsGrid_->addWidget(notFoundLabel_, row, 1);
    statsGrid_->addWidget(new QLabel("Active:"), row, 2);
    activeLabel_ = new QLabel("0");
    statsGrid_->addWidget(activeLabel_, row, 3);
    statsGrid_->addWidget(new QLabel("Wall Speed:"), row, 4);
    speedLabel_ = new QLabel("0 B/s");
    statsGrid_->addWidget(speedLabel_, row, 5);

    row++;
    statsGrid_->addWidget(new QLabel("Pages:"), row, 0);
    pagesLabel_ = new QLabel("0");
    statsGrid_->addWidget(pagesLabel_, row, 1);
    statsGrid_->addWidget(new QLabel("Downloaded:"), row, 2);
    bytesLabel_ = new QLabel("0 B");
    statsGrid_->addWidget(bytesLabel_, row, 3);
    statsGrid_->addWidget(new QLabel("Wire Speed:"), row, 4);
    wireSpeedLabel_ = new QLabel("0 B/s");
    statsGrid_->addWidget(wireSpeedLabel_, row, 5);

    downloaderLayout->addWidget(statsGroup_);

    // Scraper progress
    scraperGroup_ = new QGroupBox("Scraper Progress");
    QVBoxLayout* scraperLayout = new QVBoxLayout(scraperGroup_);
    scraperProgress_ = new QProgressBar();
    scraperProgress_->setRange(0, 100);
    scraperLayout->addWidget(scraperProgress_);
    scraperLabel_ = new QLabel("0 / 0 pages scraped");
    scraperLayout->addWidget(scraperLabel_);

    QHBoxLayout* scraperControls = new QHBoxLayout();
    scraperControls->addStretch();
    scraperPauseButton_ = new QPushButton("Pause Scraping");
    scraperPauseButton_->setEnabled(false);
    scraperPauseButton_->setToolTip("Pause/Resume only the scraper (downloads continue)");
    connect(scraperPauseButton_, &QPushButton::clicked, this, &MainWindow::onScraperPauseClicked);
    scraperControls->addWidget(scraperPauseButton_);
    scraperLayout->addLayout(scraperControls);

    downloaderLayout->addWidget(scraperGroup_);

    // Brute force progress
    bruteForceGroup_ = new QGroupBox("Brute Force Progress");
    QVBoxLayout* bfLayout = new QVBoxLayout(bruteForceGroup_);
    bruteForceProgress_ = new QProgressBar();
    bruteForceProgress_->setRange(0, 100);
    bfLayout->addWidget(bruteForceProgress_);
    bruteForceLabel_ = new QLabel("EFTA00000000 - 0.00%");
    bfLayout->addWidget(bruteForceLabel_);
    downloaderLayout->addWidget(bruteForceGroup_);

    // Log view - using QPlainTextEdit for better performance
    logGroup_ = new QGroupBox("Log");
    QVBoxLayout* logLayout = new QVBoxLayout(logGroup_);

    // Log controls row
    QHBoxLayout* logControlsLayout = new QHBoxLayout();
    logControlsLayout->addWidget(new QLabel("Verbosity:"));
    logVerbosityCombo_ = new QComboBox();
    logVerbosityCombo_->addItem("Quiet (errors only)", static_cast<int>(LogLevel::QUIET));
    logVerbosityCombo_->addItem("Normal", static_cast<int>(LogLevel::NORMAL));
    logVerbosityCombo_->addItem("Verbose (all files)", static_cast<int>(LogLevel::VERBOSE));
    logVerbosityCombo_->addItem("Debug", static_cast<int>(LogLevel::DEBUG));
    logVerbosityCombo_->setCurrentIndex(1);  // Normal by default
    connect(logVerbosityCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        logLevel_ = static_cast<LogLevel>(logVerbosityCombo_->itemData(index).toInt());
    });
    logControlsLayout->addWidget(logVerbosityCombo_);

    logControlsLayout->addSpacing(20);
    logControlsLayout->addWidget(new QLabel("Channels:"));
    logSystemCheck_ = new QCheckBox("System");
    logSystemCheck_->setChecked(true);
    logControlsLayout->addWidget(logSystemCheck_);
    logScraperCheck_ = new QCheckBox("Scraper");
    logScraperCheck_->setChecked(true);
    logControlsLayout->addWidget(logScraperCheck_);
    logDownloadCheck_ = new QCheckBox("Download");
    logDownloadCheck_->setChecked(true);
    logControlsLayout->addWidget(logDownloadCheck_);
    logDebugCheck_ = new QCheckBox("Debug");
    logDebugCheck_->setChecked(false);
    logControlsLayout->addWidget(logDebugCheck_);
    logControlsLayout->addStretch();

    QPushButton* clearLogButton = new QPushButton("Clear");
    logControlsLayout->addWidget(clearLogButton);

    logLayout->addLayout(logControlsLayout);

    logView_ = new QPlainTextEdit();
    logView_->setReadOnly(true);
    logView_->setFont(QFont("Monospace", 9));
    logView_->setMaximumBlockCount(MAX_LOG_LINES);  // Auto-trim old lines
    logView_->setLineWrapMode(QPlainTextEdit::NoWrap);
    logView_->setMinimumHeight(120);
    logLayout->addWidget(logView_);

    // Connect clear button after logView_ is created
    connect(clearLogButton, &QPushButton::clicked, logView_, &QPlainTextEdit::clear);

    downloaderLayout->addWidget(logGroup_);

    // Control buttons
    controlsLayout_ = new QHBoxLayout();
    controlsLayout_->addStretch();

    startButton_ = new QPushButton("Start");
    connect(startButton_, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    controlsLayout_->addWidget(startButton_);

    resumeButton_ = new QPushButton("Resume");
    resumeButton_->setToolTip("Resume interrupted downloads from database");
    connect(resumeButton_, &QPushButton::clicked, this, &MainWindow::onResumeClicked);
    controlsLayout_->addWidget(resumeButton_);

    retryFailedButton_ = new QPushButton("Retry Failed");
    retryFailedButton_->setToolTip("Retry all failed downloads");
    connect(retryFailedButton_, &QPushButton::clicked, this, &MainWindow::onRetryFailedClicked);
    controlsLayout_->addWidget(retryFailedButton_);

    redownloadAllButton_ = new QPushButton("Redownload All");
    redownloadAllButton_->setToolTip("Reset all files to pending and redownload (keeps scraped URLs)");
    connect(redownloadAllButton_, &QPushButton::clicked, this, &MainWindow::onRedownloadAllClicked);
    controlsLayout_->addWidget(redownloadAllButton_);

    clearDataSetButton_ = new QPushButton("Clear");
    clearDataSetButton_->setToolTip("Clear all progress for this data set and start fresh");
    connect(clearDataSetButton_, &QPushButton::clicked, this, &MainWindow::onClearDataSetClicked);
    controlsLayout_->addWidget(clearDataSetButton_);

    pauseButton_ = new QPushButton("Pause");
    pauseButton_->setEnabled(false);
    connect(pauseButton_, &QPushButton::clicked, this, &MainWindow::onPauseClicked);
    controlsLayout_->addWidget(pauseButton_);

    stopButton_ = new QPushButton("Stop");
    stopButton_->setEnabled(false);
    connect(stopButton_, &QPushButton::clicked, this, &MainWindow::onStopClicked);
    controlsLayout_->addWidget(stopButton_);

    controlsLayout_->addStretch();
    downloaderLayout->addLayout(controlsLayout_);

    // Add downloader tab
    tabWidget_->addTab(downloaderTab_, "Downloader");

    // Browser tab
    browserWidget_ = new BrowserWidget();
    tabWidget_->addTab(browserWidget_, "Browser");

    // Create scraper pool for parallel scraping
    scraperPool_ = new ScraperPool(this);
    connect(scraperPool_, &ScraperPool::pageReady,
            this, &MainWindow::onScraperPageReady);
    connect(scraperPool_, &ScraperPool::pageFailed,
            this, [this](int page, const QString& error) {
                logNormal(LogChannel::SCRAPER, QString("Page %1 failed: %2").arg(page).arg(error));
            });
    connect(scraperPool_, &ScraperPool::allPagesComplete,
            this, &MainWindow::onScrapingComplete);
    connect(scraperPool_, &ScraperPool::progressUpdate,
            this, [this](int scraped, int total) {
                if (total > 0) {
                    int pct = 100 * scraped / total;
                    updateProgressBar(scraperProgress_, pct);
                    updateLabel(scraperLabel_, QString("%1 / %2 pages scraped").arg(scraped).arg(total));
                }
            });

    // Connect browser signals
    connect(browserWidget_, &BrowserWidget::cookiesChanged, this, [this]() {
        if (browserWidget_->hasCookiesFor(QString::fromStdString(TARGET_DOMAIN))) {
            statusBar()->showMessage("Cookies updated from browser", 3000);
        }
    });

    connect(browserWidget_, &BrowserWidget::pageHtmlReady,
            this, &MainWindow::onBrowserPageReady);

    // Timer for pacing browser scraping
    scrapeTimer_ = new QTimer(this);
    scrapeTimer_->setSingleShot(true);
    scrapeTimer_->setTimerType(Qt::CoarseTimer);
    connect(scrapeTimer_, &QTimer::timeout, this, &MainWindow::scrapeNextPage);

    statusBar()->showMessage(QString("Browse to %1 to get cookies, then start download").arg(QString::fromStdString(TARGET_DOMAIN)));
}

void MainWindow::onStartClicked() {
    startDownload(selectedDataSet_, selectedMode_);
}

void MainWindow::onResumeClicked() {
    if (isRunning_.load()) return;

    QString downloadDir = downloadPathEdit_->text();
    if (downloadDir.isEmpty()) {
        downloadDir = "downloads";
    }

    // Store database in config directory for persistence
    QString configDir = QDir::homePath() + "/.config/efgrabber";
    QDir().mkpath(configDir);
    QString dbPath = configDir + "/efgrabber.db";

    downloadManager_ = std::make_unique<DownloadManager>(dbPath.toStdString(), downloadDir.toStdString());

    if (!downloadManager_->initialize()) {
        logQuiet(LogChannel::SYSTEM, "Failed to initialize download manager");
        return;
    }

    downloadManager_->set_max_concurrent_downloads(threadCountSpin_->value());
    downloadManager_->set_overwrite_existing(overwriteExistingCheck_->isChecked());

    // Set cookies if available
    if (browserWidget_->hasCookiesFor(QString::fromStdString(TARGET_DOMAIN))) {
        QString browserCookies = browserWidget_->getCookieString(QString::fromStdString(TARGET_DOMAIN));
        if (!browserCookies.isEmpty()) {
            downloadManager_->set_cookie_string(browserCookies.toStdString());
            logNormal(LogChannel::SYSTEM, "Using cookies from browser session");
        }
    }

    auto config = get_data_set_config(selectedDataSet_);

    // Reset any interrupted downloads
    int resetCount = downloadManager_->reset_interrupted_downloads(selectedDataSet_);
    if (resetCount > 0) {
        logNormal(LogChannel::SYSTEM, QString("Reset %1 interrupted downloads to pending").arg(resetCount));
    }

    if (!downloadManager_->has_pending_work(selectedDataSet_)) {
        logQuiet(LogChannel::SYSTEM, "No pending work to resume for this data set");
        downloadManager_.reset();
        return;
    }

    isRunning_.store(true);
    startButton_->setEnabled(false);
    resumeButton_->setEnabled(false);
    retryFailedButton_->setEnabled(false);
    redownloadAllButton_->setEnabled(false);
    clearDataSetButton_->setEnabled(false);
    pauseButton_->setEnabled(true);
    stopButton_->setEnabled(true);
    dataSetCombo_->setEnabled(false);
    modeCombo_->setEnabled(false);

    statsTimer_->start(2000);

    logQuiet(LogChannel::SYSTEM, QString("Resuming downloads for Data Set %1").arg(selectedDataSet_));

    downloadManager_->start_download_only(config);
}

void MainWindow::onRetryFailedClicked() {
    if (isRunning_.load()) return;

    QString downloadDir = downloadPathEdit_->text();
    if (downloadDir.isEmpty()) {
        downloadDir = "downloads";
    }

    QString configDir = QDir::homePath() + "/.config/efgrabber";
    QDir().mkpath(configDir);
    QString dbPath = configDir + "/efgrabber.db";

    downloadManager_ = std::make_unique<DownloadManager>(dbPath.toStdString(), downloadDir.toStdString());

    if (!downloadManager_->initialize()) {
        logQuiet(LogChannel::SYSTEM, "Failed to initialize download manager");
        return;
    }

    downloadManager_->set_max_concurrent_downloads(threadCountSpin_->value());

    // Set cookies if available
    if (browserWidget_->hasCookiesFor(QString::fromStdString(TARGET_DOMAIN))) {
        QString browserCookies = browserWidget_->getCookieString(QString::fromStdString(TARGET_DOMAIN));
        if (!browserCookies.isEmpty()) {
            downloadManager_->set_cookie_string(browserCookies.toStdString());
            logNormal(LogChannel::SYSTEM, "Using cookies from browser session");
        }
    }

    auto config = get_data_set_config(selectedDataSet_);

    // Reset any stuck IN_PROGRESS files first (from crashed sessions)
    int resetCount = downloadManager_->reset_interrupted_downloads(selectedDataSet_);
    if (resetCount > 0) {
        logNormal(LogChannel::SYSTEM, QString("Reset %1 interrupted downloads").arg(resetCount));
    }

    // Retry failed downloads
    int retryCount = downloadManager_->retry_failed_downloads(selectedDataSet_);
    int totalToRetry = resetCount + retryCount;

    if (totalToRetry == 0) {
        logQuiet(LogChannel::SYSTEM, "No failed or interrupted downloads to retry for this data set");
        downloadManager_.reset();
        return;
    }

    logNormal(LogChannel::SYSTEM, QString("Retrying %1 downloads (%2 failed, %3 interrupted)")
        .arg(totalToRetry).arg(retryCount).arg(resetCount));

    isRunning_.store(true);
    startButton_->setEnabled(false);
    resumeButton_->setEnabled(false);
    retryFailedButton_->setEnabled(false);
    redownloadAllButton_->setEnabled(false);
    clearDataSetButton_->setEnabled(false);
    pauseButton_->setEnabled(true);
    stopButton_->setEnabled(true);
    dataSetCombo_->setEnabled(false);
    modeCombo_->setEnabled(false);

    statsTimer_->start(2000);

    downloadManager_->start_download_only(config);
}

void MainWindow::onRedownloadAllClicked() {
    if (isRunning_.load()) return;

    QString downloadDir = downloadPathEdit_->text();
    if (downloadDir.isEmpty()) {
        downloadDir = "downloads";
    }

    QString configDir = QDir::homePath() + "/.config/efgrabber";
    QDir().mkpath(configDir);
    QString dbPath = configDir + "/efgrabber.db";

    downloadManager_ = std::make_unique<DownloadManager>(dbPath.toStdString(), downloadDir.toStdString());

    if (!downloadManager_->initialize()) {
        logQuiet(LogChannel::SYSTEM, "Failed to initialize download manager");
        return;
    }

    downloadManager_->set_max_concurrent_downloads(threadCountSpin_->value());
    downloadManager_->set_overwrite_existing(overwriteExistingCheck_->isChecked());

    // Set cookies if available
    if (browserWidget_->hasCookiesFor(QString::fromStdString(TARGET_DOMAIN))) {
        QString browserCookies = browserWidget_->getCookieString(QString::fromStdString(TARGET_DOMAIN));
        if (!browserCookies.isEmpty()) {
            downloadManager_->set_cookie_string(browserCookies.toStdString());
            logNormal(LogChannel::SYSTEM, "Using cookies from browser session");
        }
    }

    auto config = get_data_set_config(selectedDataSet_);

    // Reset ALL files to pending (keeps the URLs, just re-downloads them)
    int resetCount = downloadManager_->reset_all_to_pending(selectedDataSet_);

    if (resetCount == 0) {
        logQuiet(LogChannel::SYSTEM, "No files found for this data set - run Start first to scrape URLs");
        downloadManager_.reset();
        return;
    }

    logNormal(LogChannel::SYSTEM, QString("Redownloading %1 files for Data Set %2")
        .arg(resetCount).arg(selectedDataSet_));

    isRunning_.store(true);
    startButton_->setEnabled(false);
    resumeButton_->setEnabled(false);
    retryFailedButton_->setEnabled(false);
    redownloadAllButton_->setEnabled(false);
    clearDataSetButton_->setEnabled(false);
    pauseButton_->setEnabled(true);
    stopButton_->setEnabled(true);
    dataSetCombo_->setEnabled(false);
    modeCombo_->setEnabled(false);

    statsTimer_->start(2000);

    downloadManager_->start_download_only(config);
}

void MainWindow::onClearDataSetClicked() {
    if (isRunning_.load()) {
        logQuiet(LogChannel::SYSTEM, "Cannot clear while running - stop first");
        return;
    }

    // Create temporary download manager to access database
    QString configDir = QDir::homePath() + "/.config/efgrabber";
    QString dbPath = configDir + "/efgrabber.db";
    QString downloadDir = downloadPathEdit_->text();
    if (downloadDir.isEmpty()) downloadDir = "downloads";

    auto tempManager = std::make_unique<DownloadManager>(dbPath.toStdString(), downloadDir.toStdString());
    if (!tempManager->initialize()) {
        logQuiet(LogChannel::SYSTEM, "Failed to access database");
        return;
    }

    int deleted = tempManager->clear_data_set(selectedDataSet_);
    if (deleted >= 0) {
        logQuiet(LogChannel::SYSTEM, QString("Cleared Data Set %1: removed %2 file records")
            .arg(selectedDataSet_).arg(deleted));
        // Also clear the seen file IDs for this session
        seenFileIds_.clear();
        pdfFoundCount_.store(0);
    } else {
        logQuiet(LogChannel::SYSTEM, "Failed to clear data set");
    }
}

void MainWindow::onStopClicked() {
    stopDownload();
}

void MainWindow::onPauseClicked() {
    pauseDownload();
}

void MainWindow::onDataSetChanged(int index) {
    selectedDataSet_ = dataSetCombo_->itemData(index).toInt();
    auto config = get_data_set_config(selectedDataSet_);
    startIdSpin_->setValue(static_cast<int>(config.first_file_id));
    endIdSpin_->setValue(static_cast<int>(config.last_file_id));
}

void MainWindow::onBrowseClicked() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Download Folder",
                                                     downloadPathEdit_->text(),
                                                     QFileDialog::ShowDirsOnly);
    if (!dir.isEmpty()) {
        downloadPathEdit_->setText(dir);
    }
}

void MainWindow::onCookieBrowseClicked() {
    QString file = QFileDialog::getOpenFileName(this, "Select Cookie File",
                                                 QDir::homePath(),
                                                 "Cookie Files (*.txt);;All Files (*)");
    if (!file.isEmpty()) {
        cookieFileEdit_->setText(file);
    }
}

void MainWindow::onModeChanged(int index) {
    selectedMode_ = static_cast<OperationMode>(modeCombo_->itemData(index).toInt());
}

void MainWindow::onStatsTimer() {
    if (downloadManager_ && isRunning_.load()) {
        DownloadStats stats = downloadManager_->get_stats();
        emit statsReceived(stats);

        // Show actual active downloads
        int activeCount = stats.files_in_progress;
        if (activeCount > 0) {
            activeDownloadsLabel_->setText(QString("(%1 downloading)").arg(activeCount));
        } else {
            activeDownloadsLabel_->setText("(idle)");
        }
    }
}

void MainWindow::onThreadCountChanged(int value) {
    if (downloadManager_) {
        downloadManager_->set_max_concurrent_downloads(value);
    }
}

void MainWindow::startDownload(int dataSet, OperationMode mode) {
    if (isRunning_.load()) return;

    QString downloadDir = downloadPathEdit_->text();
    if (downloadDir.isEmpty()) {
        downloadDir = "downloads";
    }

    // Store database in config directory for persistence
    QString configDir = QDir::homePath() + "/.config/efgrabber";
    QDir().mkpath(configDir);
    QString dbPath = configDir + "/efgrabber.db";

    downloadManager_ = std::make_unique<DownloadManager>(dbPath.toStdString(), downloadDir.toStdString());

    if (!downloadManager_->initialize()) {
        logQuiet(LogChannel::SYSTEM, "Failed to initialize download manager");
        return;
    }

    // Reset any files stuck as IN_PROGRESS from previous crashed sessions
    int resetCount = downloadManager_->reset_interrupted_downloads(dataSet);
    if (resetCount > 0) {
        logNormal(LogChannel::SYSTEM, QString("Reset %1 interrupted downloads").arg(resetCount));
    }

    // Use cookies from browser or file
    if (browserWidget_->hasCookiesFor(QString::fromStdString(TARGET_DOMAIN))) {
        QString browserCookies = browserWidget_->getCookieString(QString::fromStdString(TARGET_DOMAIN));
        if (!browserCookies.isEmpty()) {
            downloadManager_->set_cookie_string(browserCookies.toStdString());
            logNormal(LogChannel::SYSTEM, "Using cookies from browser session");
        }
    } else {
        QString cookieFile = cookieFileEdit_->text();
        if (!cookieFile.isEmpty()) {
            downloadManager_->set_cookie_file(cookieFile.toStdString());
            logNormal(LogChannel::SYSTEM, "Using cookies from file: " + cookieFile);
        }
    }

    // Set callbacks - structured events only, no string logging
    DownloadCallbacks callbacks;
    callbacks.on_stats_update = [this](const DownloadStats& stats) {
        emit statsReceived(stats);
    };
    callbacks.on_page_scraped = [this](int page, int count) {
        emit pageScraped(page, count);
    };
    callbacks.on_complete = [this]() {
        emit downloadComplete();
    };
    callbacks.on_error = [this](const std::string& error) {
        emit errorOccurred(QString::fromStdString(error));
    };
    callbacks.on_file_status_change = [this](const std::string& file_id, DownloadStatus status) {
        // Only log at verbose level - this could be millions of messages
        if (logLevel_ != LogLevel::VERBOSE && logLevel_ != LogLevel::DEBUG) return;
        if (!logDownloadCheck_->isChecked()) return;

        QString statusStr;
        switch (status) {
            case DownloadStatus::COMPLETED: statusStr = "completed"; break;
            case DownloadStatus::FAILED: statusStr = "FAILED"; break;
            case DownloadStatus::NOT_FOUND: statusStr = "404"; break;
            default: return;  // Don't log other status changes
        }
        appendLog(QString("[DL]  %1: %2").arg(QString::fromStdString(file_id)).arg(statusStr));
    };
    callbacks.on_worker_state = [this](const std::string& worker_name, bool started) {
        logNormal(LogChannel::SYSTEM, QString("%1 %2")
            .arg(QString::fromStdString(worker_name))
            .arg(started ? "started" : "finished"));
    };
    downloadManager_->set_callbacks(callbacks);

    DataSetConfig config = get_data_set_config(dataSet);
    config.first_file_id = static_cast<uint64_t>(startIdSpin_->value());
    config.last_file_id = static_cast<uint64_t>(endIdSpin_->value());

    isRunning_.store(true);
    isPaused_.store(false);

    startButton_->setEnabled(false);
    resumeButton_->setEnabled(false);
    retryFailedButton_->setEnabled(false);
    clearDataSetButton_->setEnabled(false);
    stopButton_->setEnabled(true);
    pauseButton_->setEnabled(true);
    dataSetCombo_->setEnabled(false);
    modeCombo_->setEnabled(false);

    statsTimer_->start(2000);  // Update stats every 2 seconds

    // Use browser-based scraping for scraper mode
    if (mode == OperationMode::SCRAPER || mode == OperationMode::HYBRID) {
        logNormal(LogChannel::SYSTEM, "Using browser-based scraping to bypass Akamai");
        startBrowserScraping(dataSet);
        scraperPauseButton_->setEnabled(true);
        scraperPauseButton_->setText("Pause Scraping");
    }

    if (mode == OperationMode::BRUTE_FORCE || mode == OperationMode::HYBRID) {
        downloadManager_->start(config, mode);
        logNormal(LogChannel::SYSTEM, QString("Started downloading %1").arg(QString::fromStdString(config.name)));
    }
}

void MainWindow::startBrowserScraping(int dataSet) {
    browserScrapingActive_.store(true);
    pdfFoundCount_.store(0);
    seenFileIds_.clear();
    detectedLastPage_ = -1;
    detectingMaxPage_ = true;
    verifyingFirstPage_ = false;

    logNormal(LogChannel::SCRAPER, QString("Starting browser-based scraping for Data Set %1").arg(dataSet));

    auto config = get_data_set_config(dataSet);

    // Set initial thread count and options from UI
    downloadManager_->set_max_concurrent_downloads(threadCountSpin_->value());
    downloadManager_->set_overwrite_existing(overwriteExistingCheck_->isChecked());

    // Tell download manager that external scraping is active
    downloadManager_->set_external_scraping_active(true);

    downloadManager_->start_download_only(config);

    // Check if we already have detected total pages in the database
    auto stats = downloadManager_->get_stats();
    if (stats.total_pages > 0) {
        logNormal(LogChannel::SCRAPER, QString("Resume: %1 pages already known").arg(stats.total_pages));
        detectedLastPage_ = static_cast<int>(stats.total_pages - 1);
        detectingMaxPage_ = false;

        // Get unscraped pages
        std::vector<int> unscraped = downloadManager_->get_unscraped_pages(dataSet, detectedLastPage_);
        if (unscraped.empty()) {
            logNormal(LogChannel::SCRAPER, "All pages already scraped according to database.");
            onScrapingComplete();
            return;
        }

        int tabCount = scraperTabCountSpin_->value();
        scraperPool_->setPoolSize(tabCount);

#ifdef HAVE_WEBENGINE
        // Share browser profile with scraper pool for cookie sharing
        scraperPool_->setCookieProfile(browserWidget_->profile());
#endif

        QList<int> pages;
        for (int p : unscraped) pages.append(p);

        logNormal(LogChannel::SCRAPER, QString("Resuming parallel scraping for %1 unscraped pages with %2 tab(s)").arg(pages.size()).arg(tabCount));
        scraperPool_->startScrapingPages(QString::fromStdString(config.base_url), pages, detectedLastPage_ + 1);
    } else {
        // Load a high page number first - server will show the actual last page
        logNormal(LogChannel::SCRAPER, "Detecting total page count via high-page probe...");
        currentScrapePage_.store(99999);
        scrapeNextPage();
    }
}

void MainWindow::scrapeNextPage() {
    if (!browserScrapingActive_.load() || !isRunning_.load()) return;

    auto config = get_data_set_config(selectedDataSet_);
    int page = currentScrapePage_.load();
    QString url;
    if (page == 0) {
        url = QString::fromStdString(config.base_url);
    } else {
        url = QString::fromStdString(config.base_url) + "?page=" + QString::number(page);
    }

    updateLabel(scraperLabel_, QString("Fetching page %1...").arg(page));
    browserWidget_->fetchPageForScraping(url);
}

void MainWindow::stopDownload() {
    if (!isRunning_.load()) return;

    browserScrapingActive_.store(false);
    if (scrapeTimer_) scrapeTimer_->stop();
    if (scraperPool_) scraperPool_->stop();
    statsTimer_->stop();

    if (downloadManager_) {
        downloadManager_->set_external_scraping_active(false);
        downloadManager_->stop();
        downloadManager_.reset();
    }

    isRunning_.store(false);
    isPaused_.store(false);

    startButton_->setEnabled(true);
    resumeButton_->setEnabled(true);
    retryFailedButton_->setEnabled(true);
    redownloadAllButton_->setEnabled(true);
    clearDataSetButton_->setEnabled(true);
    stopButton_->setEnabled(false);
    pauseButton_->setEnabled(false);
    pauseButton_->setText("Pause");
    dataSetCombo_->setEnabled(true);
    modeCombo_->setEnabled(true);
    activeDownloadsLabel_->setText("");
    scraperPauseButton_->setEnabled(false);
    scraperPauseButton_->setText("Pause Scraping");

    logNormal(LogChannel::SYSTEM, "Download stopped");
}

void MainWindow::pauseDownload() {
    if (!isRunning_.load()) return;

    if (isPaused_.load()) {
        if (downloadManager_) downloadManager_->resume();
        if (scraperPool_) {
            scraperPool_->resume();
            scraperPauseButton_->setText("Pause Scraping");
        }
        isPaused_.store(false);
        pauseButton_->setText("Pause");
        logNormal(LogChannel::SYSTEM, "Download resumed");
    } else {
        if (downloadManager_) downloadManager_->pause();
        if (scraperPool_) {
            scraperPool_->pause();
            scraperPauseButton_->setText("Resume Scraping");
        }
        isPaused_.store(true);
        pauseButton_->setText("Resume");
        logNormal(LogChannel::SYSTEM, "Download paused");
    }
}

void MainWindow::onScraperPauseClicked() {
    if (!scraperPool_ || !browserScrapingActive_.load()) return;

    if (scraperPool_->isPaused()) {
        scraperPool_->resume();
        scraperPauseButton_->setText("Pause Scraping");
        logNormal(LogChannel::SCRAPER, "Scraper resumed");
    } else {
        scraperPool_->pause();
        scraperPauseButton_->setText("Resume Scraping");
        logNormal(LogChannel::SCRAPER, "Scraper paused");
    }
}

void MainWindow::appendLog(const QString& message) {
    QString timestamp = QDateTime::currentDateTime().toString("[HH:mm:ss] ");
    QMutexLocker locker(&logMutex_);
    pendingLogs_.append(timestamp + message);
}

bool MainWindow::shouldLog(LogLevel level, LogChannel channel) const {
    // Check verbosity level
    if (static_cast<int>(level) > static_cast<int>(logLevel_)) {
        return false;
    }

    // Check channel filter
    switch (channel) {
        case LogChannel::SYSTEM:
            return logSystemCheck_->isChecked();
        case LogChannel::SCRAPER:
            return logScraperCheck_->isChecked();
        case LogChannel::DOWNLOAD:
            return logDownloadCheck_->isChecked();
        case LogChannel::DEBUG:
            return logDebugCheck_->isChecked();
    }
    return true;
}

void MainWindow::log(LogLevel level, LogChannel channel, const QString& message) {
    if (!shouldLog(level, channel)) return;

    // Add channel prefix
    QString prefix;
    switch (channel) {
        case LogChannel::SYSTEM:   prefix = "[SYS] "; break;
        case LogChannel::SCRAPER:  prefix = "[SCR] "; break;
        case LogChannel::DOWNLOAD: prefix = "[DL]  "; break;
        case LogChannel::DEBUG:    prefix = "[DBG] "; break;
    }
    appendLog(prefix + message);
}

void MainWindow::logQuiet(LogChannel channel, const QString& message) {
    log(LogLevel::QUIET, channel, message);
}

void MainWindow::logNormal(LogChannel channel, const QString& message) {
    log(LogLevel::NORMAL, channel, message);
}

void MainWindow::logVerbose(LogChannel channel, const QString& message) {
    log(LogLevel::VERBOSE, channel, message);
}

void MainWindow::logDebug(LogChannel channel, const QString& message) {
    log(LogLevel::DEBUG, channel, message);
}

void MainWindow::flushPendingLogs() {
    QStringList logs;
    {
        QMutexLocker locker(&logMutex_);
        if (pendingLogs_.isEmpty()) return;
        logs.swap(pendingLogs_);
    }

    // Batch append all logs at once
    logView_->setUpdatesEnabled(false);
    for (const QString& log : logs) {
        logView_->appendPlainText(log);
    }
    logView_->setUpdatesEnabled(true);

    // Scroll to bottom
    QScrollBar* scrollBar = logView_->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}

void MainWindow::updateProgressBar(QProgressBar* bar, int value) {
    if (bar->value() != value) {
        bar->setValue(value);
    }
}

void MainWindow::updateLabel(QLabel* label, const QString& text) {
    if (label->text() != text) {
        label->setText(text);
    }
}

void MainWindow::updateStats(const DownloadStats& stats) {
    // Calculate total files and done files
    // "Done" includes completed, 404s, and skipped - anything that doesn't need more work
    int64_t total = stats.files_completed + stats.files_failed + stats.files_pending +
                   stats.files_in_progress + stats.files_not_found + stats.files_skipped;
    int64_t done = stats.files_completed + stats.files_not_found + stats.files_skipped;
    int progress = total > 0 ? static_cast<int>(100 * done / total) : 0;

    updateProgressBar(overallProgress_, progress);

    QString overallText = QString("%1 / %2 files (%3%)")
        .arg(done).arg(total).arg(progress);
    updateLabel(overallLabel_, overallText);

    // Stats labels - only update if changed
    updateLabel(completedLabel_, QString::number(stats.files_completed));
    updateLabel(failedLabel_, QString::number(stats.files_failed));
    updateLabel(pendingLabel_, QString::number(stats.files_pending));
    updateLabel(notFoundLabel_, QString::number(stats.files_not_found));
    updateLabel(activeLabel_, QString::number(stats.files_in_progress));
    updateLabel(pagesLabel_, QString::number(stats.pages_scraped));
    updateLabel(speedLabel_, formatSpeed(stats.current_speed_bps));
    updateLabel(wireSpeedLabel_, formatSpeed(stats.wire_speed_bps));
    updateLabel(bytesLabel_, formatBytes(stats.bytes_downloaded));

    // Scraper progress
    if (stats.total_pages > 0) {
        int scraperPct = static_cast<int>(100 * stats.pages_scraped / stats.total_pages);
        updateProgressBar(scraperProgress_, scraperPct);
        updateLabel(scraperLabel_, QString("%1 / %2 pages (%3 PDFs)")
            .arg(stats.pages_scraped).arg(stats.total_pages).arg(stats.total_files_found));
    }

    // Brute force progress
    if (stats.brute_force_end > stats.brute_force_start) {
        uint64_t range = stats.brute_force_end - stats.brute_force_start;
        uint64_t done = stats.brute_force_current - stats.brute_force_start;
        int bfPct = static_cast<int>(100.0 * done / range);
        updateProgressBar(bruteForceProgress_, bfPct);

        QString bfText = QString("EFTA%1 - %2% (%3 / %4)")
            .arg(stats.brute_force_current, 8, 10, QChar('0'))
            .arg(bfPct)
            .arg(done)
            .arg(range);
        updateLabel(bruteForceLabel_, bfText);
    }
}

void MainWindow::onBrowserPageReady(const QString& url, const QString& html) {
    Q_UNUSED(url);
    if (!browserScrapingActive_.load()) return;

    // Helper to check for anti-bot signatures
    auto checkBlocked = [](const QString& html) -> bool {
        return html.contains("Access Denied") ||
               html.contains("captcha") ||
               html.contains("Please verify") ||
               html.contains("bot detection") ||
               html.contains("I am not a robot") ||
               html.contains("reauth()") ||
               html.contains("abuse-deterrent.js") ||
               html.length() < 1000;
    };

    // Handle verification of first page (after page 99999 was blocked)
    if (verifyingFirstPage_) {
        verifyingFirstPage_ = false;

        bool isBlocked = checkBlocked(html);
        if (isBlocked) {
            // Page 0 is also blocked - this is a real anti-bot block
            logQuiet(LogChannel::SYSTEM, "CRITICAL: Anti-bot challenge detected. Please solve the challenge in the Browser tab.");
            // Still try to continue with sequential scraping after user solves it
        } else {
            logNormal(LogChannel::SCRAPER, "Page 0 loaded successfully - anti-bot only affected high page numbers");
        }

        // Continue with sequential scraping from page 0
        detectedLastPage_ = -1;
        updateLabel(scraperLabel_, "Scraping pages (unknown total)...");
        currentScrapePage_.store(0);
        // Process this page's content (it's page 0)
        // Fall through to the sequential scraping logic below
    }

    // Handle max page detection (first request to page 99999)
    if (detectingMaxPage_) {
        detectingMaxPage_ = false;

        int maxFound = -1;
        bool isBlocked = checkBlocked(html);

        if (!isBlocked) {
            // When requesting page 99999, the server redirects to the actual last page.
            // The current page is marked with aria-current="page" in the pagination.
            QRegularExpression currentPageRe(R"(<a[^>]*href=["']\?page=(\d+)["'][^>]*aria-current=["']page["']|<a[^>]*aria-current=["']page["'][^>]*href=["']\?page=(\d+)["'])");
            QRegularExpressionMatch currentMatch = currentPageRe.match(html);
            if (currentMatch.hasMatch()) {
                QString captured = currentMatch.captured(1).isEmpty() ? currentMatch.captured(2) : currentMatch.captured(1);
                maxFound = captured.toInt();
            }
        }

        if (maxFound >= 0) {
            detectedLastPage_ = maxFound;
            logNormal(LogChannel::SCRAPER, QString("Detected %1 pages total (page 0 to %2)").arg(maxFound + 1).arg(maxFound));
            updateProgressBar(scraperProgress_, 0);
            updateLabel(scraperLabel_, QString("0 / %1 pages").arg(maxFound + 1));

            // Start multi-tab scraping using the scraper pool
            auto config = get_data_set_config(selectedDataSet_);
            int tabCount = scraperTabCountSpin_->value();
            scraperPool_->setPoolSize(tabCount);

#ifdef HAVE_WEBENGINE
            // Share browser profile with scraper pool for cookie sharing
            scraperPool_->setCookieProfile(browserWidget_->profile());
#endif

            logNormal(LogChannel::SCRAPER, QString("Starting parallel scraping with %1 tab(s)").arg(tabCount));
            scraperPool_->startScraping(QString::fromStdString(config.base_url), maxFound);
        } else {
            // Couldn't detect max page - check for force override first
            if (forceParallelCheck_->isChecked()) {
                int forcedMax = forceMaxPageSpin_->value();
                detectedLastPage_ = forcedMax;
                logNormal(LogChannel::SCRAPER, QString("Page detection failed, but Force Parallel is enabled. Using max page: %1").arg(forcedMax));

                updateProgressBar(scraperProgress_, 0);
                updateLabel(scraperLabel_, QString("0 / %1 pages (Forced)").arg(forcedMax + 1));

                // Start multi-tab scraping using the scraper pool
                auto config = get_data_set_config(selectedDataSet_);
                int tabCount = scraperTabCountSpin_->value();
                scraperPool_->setPoolSize(tabCount);

#ifdef HAVE_WEBENGINE
                // Share browser profile with scraper pool for cookie sharing
                scraperPool_->setCookieProfile(browserWidget_->profile());
#endif

                logNormal(LogChannel::SCRAPER, QString("Starting forced parallel scraping with %1 tab(s)").arg(tabCount));
                scraperPool_->startScraping(QString::fromStdString(config.base_url), forcedMax);
                return;
            }

            // Couldn't detect max page - could be anti-bot or other issue
            if (isBlocked) {
                // Anti-bot triggered on page 99999 - verify with page 0 before alerting user
                logNormal(LogChannel::SCRAPER, "High page probe blocked - verifying with page 0...");
                verifyingFirstPage_ = true;
                auto config = get_data_set_config(selectedDataSet_);
                browserWidget_->fetchPageForScraping(QString::fromStdString(config.base_url));
            } else {
                // No pagination found but not blocked - just start sequential
                logNormal(LogChannel::SCRAPER, "Could not detect page count - starting from page 0, will stop at last page");
                detectedLastPage_ = -1;
                updateLabel(scraperLabel_, "Scraping pages (unknown total)...");
                currentScrapePage_.store(0);
                scrapeNextPage();
            }
        }
        return;
    }

    // Sequential scraping mode (fallback when max page detection failed)
    // This processes each page and checks for "Next" link
    auto config = get_data_set_config(selectedDataSet_);
    int dataSet = selectedDataSet_;
    QString downloadPath = downloadPathEdit_->text();
    int currentPage = currentScrapePage_.load();

    // Check for "Next" link
    bool hasNextPage = html.contains("aria-label=\"Next page\"") ||
                       html.contains("pagination__link--next") ||
                       html.contains(">Next<");

    // Copy seen IDs for thread safety
    QSet<QString> seenCopy = seenFileIds_;

    QtConcurrent::run([this, html, config, dataSet, downloadPath, currentPage, hasNextPage, seenCopy]() mutable {
        int newPdfCount = 0;
        QRegularExpression re(R"(EFTA(\d{8}))", QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatchIterator it = re.globalMatch(html);

        QSet<QString> pageIds;
        std::vector<std::tuple<std::string, std::string, std::string>> filesToAdd;

        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            QString fileId = "EFTA" + match.captured(1);

            if (!pageIds.contains(fileId)) {
                pageIds.insert(fileId);

                if (!seenCopy.contains(fileId)) {
                    newPdfCount++;

                    std::string stdFileId = fileId.toStdString();
                    std::string fileUrl = config.file_url_base + stdFileId + ".pdf";
                    std::string subdir = stdFileId.substr(4, 3);
                    std::string localPath = downloadPath.toStdString() +
                        "/DataSet" + std::to_string(dataSet) + "/" +
                        subdir + "/" + stdFileId + ".pdf";

                    filesToAdd.emplace_back(stdFileId, fileUrl, localPath);
                }
            }
        }

        if (downloadManager_ && !filesToAdd.empty()) {
            std::cerr << "[DEBUG GUI] Adding " << filesToAdd.size() << " files to queue from page " << currentPage << std::endl;
            downloadManager_->add_files_to_queue(filesToAdd);
        }

        // Mark page as scraped in database (sequential fallback mode)
        if (downloadManager_) {
            downloadManager_->mark_page_scraped(dataSet, currentPage, newPdfCount);
        }

        QMetaObject::invokeMethod(this, [this, newPdfCount, currentPage, hasNextPage, pageIds]() {
            if (!browserScrapingActive_.load()) return;

            seenFileIds_.unite(pageIds);
            int newTotal = pdfFoundCount_.fetch_add(newPdfCount) + newPdfCount;

            updateLabel(scraperLabel_, QString("Page %1 - %2 PDFs total").arg(currentPage + 1).arg(newTotal));
            logNormal(LogChannel::SCRAPER, QString("Page %1: %2 new PDFs (total: %3)")
                .arg(currentPage).arg(newPdfCount).arg(newTotal));

            if (!hasNextPage) {
                // No more pages - scraping complete
                logQuiet(LogChannel::SCRAPER, QString("Scraping complete! %1 unique PDFs across %2 pages")
                    .arg(newTotal).arg(currentPage + 1));
                browserScrapingActive_.store(false);

                if (downloadManager_) {
                    downloadManager_->set_external_scraping_active(false);
                }
                return;
            }

            // Continue to next page
            currentScrapePage_.fetch_add(1);
            scrapeTimer_->start(1500);  // Wait 1.5s between pages to avoid rate limiting
        }, Qt::QueuedConnection);
    });
}

void MainWindow::onScraperPageReady(int pageNumber, const QString& html) {
    if (!browserScrapingActive_.load()) return;

    auto config = get_data_set_config(selectedDataSet_);
    int dataSet = selectedDataSet_;
    QString downloadPath = downloadPathEdit_->text();

    // Copy seen IDs for thread safety
    QSet<QString> seenCopy = seenFileIds_;

    QtConcurrent::run([this, html, config, dataSet, downloadPath, pageNumber, seenCopy]() mutable {
        int newPdfCount = 0;
        QRegularExpression re(R"(EFTA(\d{8}))", QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatchIterator it = re.globalMatch(html);

        QSet<QString> pageIds;
        std::vector<std::tuple<std::string, std::string, std::string>> filesToAdd;

        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            QString fileId = "EFTA" + match.captured(1);

            if (!pageIds.contains(fileId)) {
                pageIds.insert(fileId);

                if (!seenCopy.contains(fileId)) {
                    newPdfCount++;

                    std::string stdFileId = fileId.toStdString();
                    std::string fileUrl = config.file_url_base + stdFileId + ".pdf";
                    std::string subdir = stdFileId.substr(4, 3);
                    std::string localPath = downloadPath.toStdString() +
                        "/DataSet" + std::to_string(dataSet) + "/" +
                        subdir + "/" + stdFileId + ".pdf";

                    filesToAdd.emplace_back(stdFileId, fileUrl, localPath);
                }
            }
        }

        if (downloadManager_ && !filesToAdd.empty()) {
            std::cerr << "[DEBUG GUI] Adding " << filesToAdd.size() << " files to queue from page " << pageNumber << std::endl;
            downloadManager_->add_files_to_queue(filesToAdd);
        }

        // Mark page as scraped in database
        if (downloadManager_) {
            downloadManager_->mark_page_scraped(dataSet, pageNumber, newPdfCount);
        }

        QMetaObject::invokeMethod(this, [this, newPdfCount, pageNumber, pageIds]() {
            if (!browserScrapingActive_.load()) return;

            seenFileIds_.unite(pageIds);
            int newTotal = pdfFoundCount_.fetch_add(newPdfCount) + newPdfCount;

            logNormal(LogChannel::SCRAPER, QString("Page %1: %2 new PDFs (total: %3)")
                .arg(pageNumber).arg(newPdfCount).arg(newTotal));
        }, Qt::QueuedConnection);
    });
}

void MainWindow::onScrapingComplete() {
    if (!browserScrapingActive_.load()) return;

    int totalPdfs = pdfFoundCount_.load();
    int totalPages = scraperPool_->totalPages();

    logQuiet(LogChannel::SCRAPER, QString("Scraping complete! %1 unique PDFs across %2 pages")
        .arg(totalPdfs).arg(totalPages));

    browserScrapingActive_.store(false);

    // Tell download manager that external scraping is done
    if (downloadManager_) {
        downloadManager_->set_external_scraping_active(false);
    }
}

void MainWindow::handlePageScraped(int page, int count) {
    logNormal(LogChannel::SCRAPER, QString("Scraped page %1 (%2 PDFs)").arg(page).arg(count));
}

void MainWindow::handleDownloadComplete() {
    logQuiet(LogChannel::SYSTEM, "Download complete!");
    stopDownload();
}

void MainWindow::handleError(const QString& error) {
    logQuiet(LogChannel::SYSTEM, "ERROR: " + error);
}

void MainWindow::processBrowserHtml(const QString& html) {
    Q_UNUSED(html);
}

QString MainWindow::formatBytes(int64_t bytes) const {
    static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unitIndex = 0;
    double value = static_cast<double>(bytes);

    while (value >= 1024.0 && unitIndex < 4) {
        value /= 1024.0;
        unitIndex++;
    }

    return QString("%1 %2").arg(value, 0, 'f', 1).arg(units[unitIndex]);
}

QString MainWindow::formatSpeed(double bps) const {
    return formatBytes(static_cast<int64_t>(bps)) + "/s";
}

} // namespace efgrabber
