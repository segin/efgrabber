/*
 * src/gui/scraper_pool.h - Manages a pool of browser views for parallel scraping
 * Copyright (c) 2026 Kirn Gill II
 * SPDX-License-Identifier: MIT
 * See LICENSE file for full license text.
 */

#ifndef EFGRABBER_SCRAPER_POOL_H
#define EFGRABBER_SCRAPER_POOL_H

#include <QObject>
#include <QVector>
#include <QList>
#include <QSet>
#include <QMutex>
#include <functional>

#ifdef HAVE_WEBENGINE
#include <QWebEngineView>
#include <QWebEngineProfile>
#include <QWebEnginePage>
#endif

namespace efgrabber {

// A pool of browser views for parallel page scraping
class ScraperPool : public QObject {
    Q_OBJECT

public:
    explicit ScraperPool(QWidget* parent = nullptr);
    ~ScraperPool() override;

    // Set the number of concurrent scrapers (1-10)
    // Can be changed at runtime - reduction waits for tabs to finish
    void setPoolSize(int size);
    int poolSize() const { return targetPoolSize_; }
    int activePoolSize() const { return views_.size(); }

    // Start scraping specific pages
    void startScrapingPages(const QString& baseUrl, const QList<int>& pages, int totalExpected);

    // Start scraping pages from 0 to maxPage
    void startScraping(const QString& baseUrl, int maxPage);

    // Stop all scraping and clear state
    void stop();

    // Check if scraping is active
    bool isActive() const { return active_; }

    // Get progress
    int pagesScraped() const;
    int totalPages() const { return maxPage_ + 1; }

    // Share cookies from a profile
    void setCookieProfile(QWebEngineProfile* profile);

signals:
    // Emitted when a page's HTML is ready
    void pageReady(int pageNumber, const QString& html);

    // Emitted when a page fails to load
    void pageFailed(int pageNumber, const QString& error);

    // Emitted when all pages are done
    void allPagesComplete();

    // Progress update
    void progressUpdate(int scraped, int total);

private slots:
    void onLoadFinished(bool ok);
    void dispatchNext();

private:
    struct ScraperView {
#ifdef HAVE_WEBENGINE
        QWebEngineView* view = nullptr;
#endif
        int currentPage = -1;
        bool busy = false;
    };

    void createViews(int count);
    void destroyViews();
    void trimExcessViews();  // Remove idle views when reducing pool size
    int getNextPage();  // Returns -1 if no more pages

    QWidget* parentWidget_;
    int targetPoolSize_ = 1;  // Desired pool size (may differ during reduction)
    QVector<ScraperView> views_;

    // Scraping state
    bool active_ = false;
    QString baseUrl_;
    int maxPage_ = -1;
    mutable QMutex mutex_;
    QSet<int> scrapedPages_;      // Pages that have been scraped successfully
    QSet<int> inProgressPages_;   // Pages currently being scraped
    QSet<int> failedPages_;       // Pages that failed (will be retried)
    QList<int> pendingPages_;     // Pages waiting to be assigned
    int nextPageToAssign_ = 0;    // Next page to assign to a free view
    int maxRetries_ = 3;          // Max retries per page

    QWebEngineProfile* sharedProfile_ = nullptr;
};

} // namespace efgrabber

#endif // EFGRABBER_SCRAPER_POOL_H
