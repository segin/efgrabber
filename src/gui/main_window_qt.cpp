#include "main_window_qt.h"
#include <QDateTime>
#include <QScrollBar>
#include <QApplication>
#include <QFileDialog>
#include <QStatusBar>
#include <QRegularExpression>
#include <QSet>
#include <sstream>
#include <iomanip>

namespace efgrabber {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , selectedDataSet_(11)
    , selectedMode_(OperationMode::SCRAPER)
    , isRunning_(false)
    , isPaused_(false)
    , browserScrapingActive_(false)
    , currentScrapePage_(0)
    , maxScrapePage_(20000)
    , pdfFoundCount_(0)
    , scrapeTimer_(nullptr)
{
    setWindowTitle("Epstein Files Grabber");
    resize(1000, 700);

    setupUi();

    // Connect signals
    connect(this, &MainWindow::logMessageReceived, this, &MainWindow::appendLog, Qt::QueuedConnection);
    connect(this, &MainWindow::statsReceived, this, &MainWindow::updateStats, Qt::QueuedConnection);
    connect(this, &MainWindow::pageScraped, this, &MainWindow::handlePageScraped, Qt::QueuedConnection);
    connect(this, &MainWindow::downloadComplete, this, &MainWindow::handleDownloadComplete, Qt::QueuedConnection);
    connect(this, &MainWindow::errorOccurred, this, &MainWindow::handleError, Qt::QueuedConnection);

    // Stats timer for periodic UI updates
    statsTimer_ = new QTimer(this);
    connect(statsTimer_, &QTimer::timeout, this, &MainWindow::onStatsTimer);
}

MainWindow::~MainWindow() {
    stopDownload();
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
    downloaderLayout->setSpacing(12);

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

    // Stats grid
    statsGroup_ = new QGroupBox("Statistics");
    statsGrid_ = new QGridLayout(statsGroup_);
    statsGrid_->setColumnStretch(1, 1);
    statsGrid_->setColumnStretch(3, 1);
    statsGrid_->setColumnStretch(5, 1);

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
    statsGrid_->addWidget(new QLabel("Not Found:"), row, 0);
    notFoundLabel_ = new QLabel("0");
    statsGrid_->addWidget(notFoundLabel_, row, 1);

    statsGrid_->addWidget(new QLabel("Active:"), row, 2);
    activeLabel_ = new QLabel("0");
    statsGrid_->addWidget(activeLabel_, row, 3);

    statsGrid_->addWidget(new QLabel("Pages:"), row, 4);
    pagesLabel_ = new QLabel("0");
    statsGrid_->addWidget(pagesLabel_, row, 5);

    row++;
    statsGrid_->addWidget(new QLabel("Speed:"), row, 0);
    speedLabel_ = new QLabel("0 B/s");
    statsGrid_->addWidget(speedLabel_, row, 1);

    statsGrid_->addWidget(new QLabel("Downloaded:"), row, 2);
    bytesLabel_ = new QLabel("0 B");
    statsGrid_->addWidget(bytesLabel_, row, 3);

    downloaderLayout->addWidget(statsGroup_);

    // Scraper progress
    scraperGroup_ = new QGroupBox("Scraper Progress");
    QVBoxLayout* scraperLayout = new QVBoxLayout(scraperGroup_);
    scraperProgress_ = new QProgressBar();
    scraperProgress_->setRange(0, 100);
    scraperLayout->addWidget(scraperProgress_);
    scraperLabel_ = new QLabel("0 / 0 pages scraped");
    scraperLayout->addWidget(scraperLabel_);
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

    // Log view
    logGroup_ = new QGroupBox("Log");
    QVBoxLayout* logLayout = new QVBoxLayout(logGroup_);
    logView_ = new QTextEdit();
    logView_->setReadOnly(true);
    logView_->setFont(QFont("Monospace", 9));
    logView_->setMinimumHeight(150);
    logLayout->addWidget(logView_);
    downloaderLayout->addWidget(logGroup_);

    // Control buttons
    controlsLayout_ = new QHBoxLayout();
    controlsLayout_->addStretch();

    startButton_ = new QPushButton("Start");
    startButton_->setStyleSheet("background-color: #4CAF50; color: white; padding: 8px 24px;");
    connect(startButton_, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    controlsLayout_->addWidget(startButton_);

    pauseButton_ = new QPushButton("Pause");
    pauseButton_->setEnabled(false);
    connect(pauseButton_, &QPushButton::clicked, this, &MainWindow::onPauseClicked);
    controlsLayout_->addWidget(pauseButton_);

    stopButton_ = new QPushButton("Stop");
    stopButton_->setStyleSheet("background-color: #f44336; color: white; padding: 8px 24px;");
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

    // Connect browser signals - cookies are transferred automatically
    connect(browserWidget_, &BrowserWidget::cookiesChanged, this, [this]() {
        if (browserWidget_->hasCookiesFor("justice.gov")) {
            statusBar()->showMessage("Cookies updated from browser", 3000);
        }
    });

    connect(browserWidget_, &BrowserWidget::pdfLinkFound, this, [this](const QString& fileId, const QString&) {
        appendLog("Found PDF: " + fileId);
    });

    // Connect browser page ready for scraping
    connect(browserWidget_, &BrowserWidget::pageHtmlReady,
            this, &MainWindow::onBrowserPageReady);

    // Timer for pacing browser scraping
    scrapeTimer_ = new QTimer(this);
    scrapeTimer_->setSingleShot(true);
    connect(scrapeTimer_, &QTimer::timeout, this, &MainWindow::scrapeNextPage);

    // Add status bar
    statusBar()->showMessage("Browse to justice.gov to get cookies, then start download");
}

void MainWindow::onStartClicked() {
    startDownload(selectedDataSet_, selectedMode_);
}

void MainWindow::onStopClicked() {
    stopDownload();
}

void MainWindow::onPauseClicked() {
    pauseDownload();
}

void MainWindow::onDataSetChanged(int index) {
    selectedDataSet_ = dataSetCombo_->itemData(index).toInt();
    // Update brute force range for this data set
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
    if (downloadManager_ && isRunning_) {
        DownloadStats stats = downloadManager_->get_stats();
        emit statsReceived(stats);
    }
}

void MainWindow::startDownload(int dataSet, OperationMode mode) {
    if (isRunning_) return;

    QString downloadDir = downloadPathEdit_->text();
    if (downloadDir.isEmpty()) {
        downloadDir = "downloads";
    }
    QString dbPath = "efgrabber.db";

    downloadManager_ = std::make_unique<DownloadManager>(dbPath.toStdString(), downloadDir.toStdString());

    if (!downloadManager_->initialize()) {
        appendLog("Failed to initialize download manager");
        return;
    }

    // First, try to use cookies from the browser
    if (browserWidget_->hasCookiesFor("justice.gov")) {
        QString browserCookies = browserWidget_->getCookieString("justice.gov");
        if (!browserCookies.isEmpty()) {
            downloadManager_->set_cookie_string(browserCookies.toStdString());
            appendLog("Using cookies from browser session");
        }
    }
    // Fall back to cookie file if specified and no browser cookies
    else {
        QString cookieFile = cookieFileEdit_->text();
        if (!cookieFile.isEmpty()) {
            downloadManager_->set_cookie_file(cookieFile.toStdString());
            appendLog("Using cookies from file: " + cookieFile);
        }
    }

    // Set callbacks
    DownloadCallbacks callbacks;

    callbacks.on_log_message = [this](const std::string& message) {
        emit logMessageReceived(QString::fromStdString(message));
    };

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

    downloadManager_->set_callbacks(callbacks);

    // Get config with user-specified brute force range
    DataSetConfig config = get_data_set_config(dataSet);
    config.first_file_id = static_cast<uint64_t>(startIdSpin_->value());
    config.last_file_id = static_cast<uint64_t>(endIdSpin_->value());

    isRunning_ = true;
    isPaused_ = false;

    startButton_->setEnabled(false);
    stopButton_->setEnabled(true);
    pauseButton_->setEnabled(true);
    dataSetCombo_->setEnabled(false);
    modeCombo_->setEnabled(false);

    statsTimer_->start(1000);

    // For scraper mode, use browser-based scraping to bypass Akamai
    if (mode == OperationMode::SCRAPER || mode == OperationMode::HYBRID) {
        appendLog("Using browser-based scraping to bypass Akamai bot protection");
        startBrowserScraping(dataSet);
    }

    // For brute force, start the download manager directly
    if (mode == OperationMode::BRUTE_FORCE || mode == OperationMode::HYBRID) {
        downloadManager_->start(config, mode);
        appendLog(QString("Started downloading %1").arg(QString::fromStdString(config.name)));
    }
}

void MainWindow::stopDownload() {
    if (!isRunning_) return;

    // Stop browser scraping
    browserScrapingActive_ = false;
    if (scrapeTimer_) {
        scrapeTimer_->stop();
    }

    statsTimer_->stop();
    if (downloadManager_) {
        downloadManager_->stop();
        downloadManager_.reset();
    }

    isRunning_ = false;
    isPaused_ = false;

    startButton_->setEnabled(true);
    stopButton_->setEnabled(false);
    pauseButton_->setEnabled(false);
    pauseButton_->setText("Pause");
    dataSetCombo_->setEnabled(true);
    modeCombo_->setEnabled(true);

    appendLog("Download stopped");
}

void MainWindow::pauseDownload() {
    if (!isRunning_ || !downloadManager_) return;

    if (isPaused_) {
        downloadManager_->resume();
        isPaused_ = false;
        pauseButton_->setText("Pause");
        appendLog("Download resumed");
    } else {
        downloadManager_->pause();
        isPaused_ = true;
        pauseButton_->setText("Resume");
        appendLog("Download paused");
    }
}

void MainWindow::appendLog(const QString& message) {
    QString timestamp = QDateTime::currentDateTime().toString("[HH:mm:ss] ");
    logView_->append(timestamp + message);

    // Scroll to bottom
    QScrollBar* scrollBar = logView_->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}

void MainWindow::updateStats(const DownloadStats& stats) {
    // Overall progress
    int64_t total = stats.files_completed + stats.files_failed + stats.files_pending +
                   stats.files_in_progress + stats.files_not_found;
    int progress = total > 0 ? static_cast<int>(100 * stats.files_completed / total) : 0;
    overallProgress_->setValue(progress);

    overallLabel_->setText(QString("%1 / %2 files (%3%)")
        .arg(stats.files_completed)
        .arg(total)
        .arg(progress));

    // Stats labels
    completedLabel_->setText(QString::number(stats.files_completed));
    failedLabel_->setText(QString::number(stats.files_failed));
    pendingLabel_->setText(QString::number(stats.files_pending));
    notFoundLabel_->setText(QString::number(stats.files_not_found));
    activeLabel_->setText(QString::number(stats.files_in_progress));
    pagesLabel_->setText(QString::number(stats.pages_scraped));
    speedLabel_->setText(formatSpeed(stats.current_speed_bps));
    bytesLabel_->setText(formatBytes(stats.bytes_downloaded));

    // Scraper progress
    if (stats.total_pages > 0) {
        int scraperPct = static_cast<int>(100 * stats.pages_scraped / stats.total_pages);
        scraperProgress_->setValue(scraperPct);
        scraperLabel_->setText(QString("%1 / %2 pages scraped (%3 PDFs found)")
            .arg(stats.pages_scraped)
            .arg(stats.total_pages)
            .arg(stats.total_files_found));
    }

    // Brute force progress
    if (stats.brute_force_end > stats.brute_force_start) {
        uint64_t range = stats.brute_force_end - stats.brute_force_start;
        uint64_t done = stats.brute_force_current - stats.brute_force_start;
        double bfPct = 100.0 * done / range;
        bruteForceProgress_->setValue(static_cast<int>(bfPct));

        std::ostringstream oss;
        oss << "EFTA" << std::setw(8) << std::setfill('0') << stats.brute_force_current
            << " - " << std::fixed << std::setprecision(2) << bfPct << "%"
            << " (" << done << " / " << range << ")";
        bruteForceLabel_->setText(QString::fromStdString(oss.str()));
    }
}

void MainWindow::handlePageScraped(int page, int count) {
    appendLog(QString("Scraped page %1 (%2 PDFs)").arg(page).arg(count));
}

void MainWindow::handleDownloadComplete() {
    appendLog("Download complete!");
    stopDownload();
}

void MainWindow::handleError(const QString& error) {
    appendLog("ERROR: " + error);
}

QString MainWindow::formatBytes(int64_t bytes) const {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unitIndex = 0;
    double value = static_cast<double>(bytes);

    while (value >= 1024.0 && unitIndex < 4) {
        value /= 1024.0;
        unitIndex++;
    }

    return QString("%1 %2").arg(value, 0, 'f', 2).arg(units[unitIndex]);
}

QString MainWindow::formatSpeed(double bps) const {
    return formatBytes(static_cast<int64_t>(bps)) + "/s";
}

void MainWindow::startBrowserScraping(int dataSet) {
    browserScrapingActive_ = true;
    currentScrapePage_ = 0;
    pdfFoundCount_ = 0;

    appendLog(QString("Starting browser-based scraping for Data Set %1").arg(dataSet));
    appendLog("Using embedded browser to bypass Akamai bot protection...");

    // Set up the config for this data set
    auto config = get_data_set_config(dataSet);

    // Start the download manager in download-only mode
    // It will wait for files to be added via browser scraping
    downloadManager_->start_download_only(config);

    // Start with first page
    scrapeNextPage();
}

void MainWindow::scrapeNextPage() {
    if (!browserScrapingActive_ || !isRunning_) return;

    auto config = get_data_set_config(selectedDataSet_);
    QString url;
    if (currentScrapePage_ == 0) {
        url = QString::fromStdString(config.base_url);
    } else {
        url = QString::fromStdString(config.base_url) + "?page=" + QString::number(currentScrapePage_);
    }

    scraperLabel_->setText(QString("Fetching page %1...").arg(currentScrapePage_));
    browserWidget_->fetchPageForScraping(url);
}

void MainWindow::onBrowserPageReady(const QString& url, const QString& html) {
    Q_UNUSED(url);
    if (!browserScrapingActive_) return;

    // Check if this is a valid page with PDFs
    int pdfCount = 0;
    QRegularExpression re(R"(EFTA(\d{8}))", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = re.globalMatch(html);

    QSet<QString> uniqueIds;
    auto config = get_data_set_config(selectedDataSet_);

    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString fileId = "EFTA" + match.captured(1);
        if (!uniqueIds.contains(fileId)) {
            uniqueIds.insert(fileId);
            pdfCount++;

            // Add to database for download
            if (downloadManager_) {
                std::string stdFileId = fileId.toStdString();
                std::string fileUrl = config.file_url_base + stdFileId + ".pdf";

                // Generate local path
                std::string subdir = stdFileId.substr(4, 3);  // First 3 digits of number
                std::string localPath = downloadPathEdit_->text().toStdString() +
                    "/DataSet" + std::to_string(selectedDataSet_) + "/" +
                    subdir + "/" + stdFileId + ".pdf";

                downloadManager_->add_file_to_queue(stdFileId, fileUrl, localPath);
            }
        }
    }

    pdfFoundCount_ += pdfCount;
    appendLog(QString("Page %1: found %2 PDFs (total: %3)")
        .arg(currentScrapePage_).arg(pdfCount).arg(pdfFoundCount_));

    scraperProgress_->setValue(currentScrapePage_ % 100);
    scraperLabel_->setText(QString("Page %1 scraped - %2 PDFs found total")
        .arg(currentScrapePage_).arg(pdfFoundCount_));

    // Check if we should continue
    if (pdfCount == 0 && currentScrapePage_ > 0) {
        // No PDFs on this page - might be the end
        appendLog("No PDFs found on page - checking if this is the last page...");

        // Check if page contains "no results" or is empty
        if (html.contains("No files found") || html.contains("Page not found") ||
            html.length() < 5000) {
            appendLog(QString("Scraping complete! Found %1 PDFs across %2 pages")
                .arg(pdfFoundCount_).arg(currentScrapePage_));
            browserScrapingActive_ = false;
            return;
        }
    }

    // Continue to next page (with delay to avoid hammering)
    currentScrapePage_++;
    if (currentScrapePage_ < maxScrapePage_) {
        scrapeTimer_->start(2000);  // 2 second delay between pages
    } else {
        appendLog("Reached max page limit");
        browserScrapingActive_ = false;
    }
}

void MainWindow::processBrowserHtml(const QString& html) {
    // Extract PDF links from HTML and add to download queue
    QRegularExpression re(R"(href\s*=\s*["']([^"']*EFTA\d{8}[^"']*\.pdf)["'])",
                          QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = re.globalMatch(html);

    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString href = match.captured(1);
        appendLog("Found: " + href);
    }
}

} // namespace efgrabber
