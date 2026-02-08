/*
 * src/gui/scraper_pool.cpp - Implementation of the scraper pool
 * Copyright (c) 2026 Kirn Gill II
 * SPDX-License-Identifier: MIT
 * See LICENSE file for full license text.
 */

#include "scraper_pool.h"
#include <QTimer>
#include <QUrl>

namespace efgrabber {

ScraperPool::ScraperPool(QWidget* parent)
    : QObject(parent), parentWidget_(parent) {
}

ScraperPool::~ScraperPool() {
    stop();
    destroyViews();
}

void ScraperPool::setPoolSize(int size) {
    size = qBound(1, size, 10);
    targetPoolSize_ = size;

    if (!active_) {
        // Not active - can resize immediately
        if (static_cast<int>(views_.size()) != size) {
            destroyViews();
            createViews(size);
        }
    } else {
        // Active - handle dynamically
        if (size > static_cast<int>(views_.size())) {
            // Growing - add more views immediately
            int toAdd = size - views_.size();
            for (int i = 0; i < toAdd; ++i) {
#ifdef HAVE_WEBENGINE
                ScraperView sv;
                sv.view = new QWebEngineView();
                sv.view->setVisible(false);
                sv.view->resize(1024, 768);
                if (sharedProfile_) {
                    QWebEnginePage* page = new QWebEnginePage(sharedProfile_, sv.view);
                    sv.view->setPage(page);
                }
                connect(sv.view, &QWebEngineView::loadFinished,
                        this, &ScraperPool::onLoadFinished);
                views_.append(sv);
#endif
            }
            // Dispatch work to new views
            dispatchNext();
        }
        // Shrinking - trimExcessViews() will be called after each view finishes
    }
}

void ScraperPool::createViews(int count) {
#ifdef HAVE_WEBENGINE
    views_.reserve(count);
    for (int i = 0; i < count; ++i) {
        ScraperView sv;
        sv.view = new QWebEngineView();
        sv.view->setVisible(false);  // Hidden - just for scraping
        sv.view->resize(1024, 768);  // Reasonable size for rendering

        // Use shared profile if available (for cookies)
        if (sharedProfile_) {
            QWebEnginePage* page = new QWebEnginePage(sharedProfile_, sv.view);
            sv.view->setPage(page);
        }

        connect(sv.view, &QWebEngineView::loadFinished,
                this, &ScraperPool::onLoadFinished);

        views_.append(sv);
    }
#else
    Q_UNUSED(count);
#endif
}

void ScraperPool::destroyViews() {
#ifdef HAVE_WEBENGINE
    for (auto& sv : views_) {
        if (sv.view) {
            sv.view->disconnect();
            sv.view->deleteLater();
        }
    }
    views_.clear();
#endif
}

void ScraperPool::trimExcessViews() {
#ifdef HAVE_WEBENGINE
    // Remove idle views if we have more than target
    while (static_cast<int>(views_.size()) > targetPoolSize_) {
        // Find an idle view to remove
        int idleIdx = -1;
        for (int i = views_.size() - 1; i >= 0; --i) {
            if (!views_[i].busy) {
                idleIdx = i;
                break;
            }
        }
        if (idleIdx < 0) {
            break;  // All views are busy, wait for them to finish
        }

        // Remove the idle view
        if (views_[idleIdx].view) {
            views_[idleIdx].view->disconnect();
            views_[idleIdx].view->deleteLater();
        }
        views_.remove(idleIdx);
    }
#endif
}

void ScraperPool::setCookieProfile(QWebEngineProfile* profile) {
    sharedProfile_ = profile;

#ifdef HAVE_WEBENGINE
    // Update existing views to use this profile
    for (auto& sv : views_) {
        if (sv.view && !sv.busy) {
            QWebEnginePage* page = new QWebEnginePage(profile, sv.view);
            sv.view->setPage(page);
        }
    }
#endif
}

void ScraperPool::startScraping(const QString& baseUrl, int maxPage) {
    QList<int> pages;
    for (int i = 0; i <= maxPage; ++i) {
        pages.append(i);
    }
    startScrapingPages(baseUrl, pages, maxPage + 1);
}

void ScraperPool::startScrapingPages(const QString& baseUrl, const QList<int>& pages, int totalExpected) {
    stop();  // Clear any previous state

    baseUrl_ = baseUrl;
    maxPage_ = totalExpected - 1;

    {
        QMutexLocker lock(&mutex_);
        pendingPages_ = pages;
        nextPageToAssign_ = 0; // Not used with explicit list but reset for safety
        scrapedPages_.clear();
        inProgressPages_.clear();
        failedPages_.clear();
        active_ = true;
    }

    // Ensure we have views
    if (static_cast<int>(views_.size()) != targetPoolSize_) {
        destroyViews();
        createViews(targetPoolSize_);
    }

    // Start dispatching pages to views
    dispatchNext();
}

void ScraperPool::stop() {
    active_ = false;
    paused_ = false;

#ifdef HAVE_WEBENGINE
    for (auto& sv : views_) {
        if (sv.busy && sv.view) {
            sv.view->stop();
        }
        sv.busy = false;
        sv.currentPage = -1;
    }
#endif

    scrapedPages_.clear();
    inProgressPages_.clear();
    failedPages_.clear();
    pendingPages_.clear();
}

void ScraperPool::pause() {
    QMutexLocker lock(&mutex_);
    paused_ = true;
}

void ScraperPool::resume() {
    {
        QMutexLocker lock(&mutex_);
        if (!active_) return; // Can't resume if not active
        paused_ = false;
    }
    dispatchNext();
}

int ScraperPool::pagesScraped() const {
    QMutexLocker lock(&mutex_);
    return scrapedPages_.size();
}

int ScraperPool::getNextPage() {
    // First, try to get a page from the explicit pending list
    if (!pendingPages_.isEmpty()) {
        int page = pendingPages_.takeFirst();
        if (!scrapedPages_.contains(page) && !inProgressPages_.contains(page)) {
            return page;
        }
        // If already scraped/in progress, try next in list
        return getNextPage();
    }

    // Fall back to range-based assignment if no list
    while (nextPageToAssign_ <= maxPage_) {
        int page = nextPageToAssign_++;
        if (!scrapedPages_.contains(page) && !inProgressPages_.contains(page)) {
            return page;
        }
    }

    // If no new pages, try a failed page (retry)
    if (!failedPages_.isEmpty()) {
        int page = *failedPages_.begin();
        failedPages_.remove(page);
        return page;
    }

    return -1;  // No more pages
}

void ScraperPool::dispatchNext() {
#ifdef HAVE_WEBENGINE
    if (!active_) return;

    QMutexLocker lock(&mutex_);
    if (paused_) return;

    for (auto& sv : views_) {
        if (!sv.busy) {
            int page = getNextPage();
            if (page < 0) {
                continue;  // No more pages to assign
            }

            sv.currentPage = page;
            sv.busy = true;
            inProgressPages_.insert(page);

            QString url;
            if (page == 0) {
                url = baseUrl_;
            } else {
                url = baseUrl_ + "?page=" + QString::number(page);
            }

            lock.unlock();
            sv.view->load(QUrl(url));
            lock.relock();
        }
    }
#endif
}

void ScraperPool::onLoadFinished(bool ok) {
#ifdef HAVE_WEBENGINE
    QWebEngineView* view = qobject_cast<QWebEngineView*>(sender());
    if (!view) return;

    ScraperView* sv = nullptr;
    int pageNumber = -1;
    {
        QMutexLocker lock(&mutex_);
        for (auto& v : views_) {
            if (v.view == view) {
                sv = &v;
                pageNumber = v.currentPage;
                break;
            }
        }
    }

    if (!sv || !sv->busy || pageNumber < 0) return;

    if (!ok) {
        {
            QMutexLocker lock(&mutex_);
            sv->busy = false;
            inProgressPages_.remove(pageNumber);
            // Add to failed pages for retry
            failedPages_.insert(pageNumber);
        }

        emit pageFailed(pageNumber, "Failed to load page");

        // Trim excess views if pool size was reduced
        trimExcessViews();

        // Try to dispatch more (including retry of failed page)
        QTimer::singleShot(1000, this, &ScraperPool::dispatchNext);  // Wait 1s before retry
        return;
    }

    // Get HTML content
    view->page()->toHtml([this, sv, pageNumber](const QString& html) {
        {
            QMutexLocker lock(&mutex_);
            sv->busy = false;
            inProgressPages_.remove(pageNumber);
            scrapedPages_.insert(pageNumber);
        }

        emit pageReady(pageNumber, html);
        emit progressUpdate(pagesScraped(), totalPages());

        // Trim excess views if pool size was reduced
        trimExcessViews();

        // Check if all done
        {
            QMutexLocker lock(&mutex_);
            int total = maxPage_ + 1;
            if (static_cast<int>(scrapedPages_.size()) == total &&
                inProgressPages_.isEmpty() && pendingPages_.isEmpty()) {
                active_ = false;
                lock.unlock();
                emit allPagesComplete();
                return;
            }
        }

        // Dispatch next page
        if (!paused_) {
            QTimer::singleShot(0, this, &ScraperPool::dispatchNext);
        }
    });
#else
    Q_UNUSED(ok);
#endif
}

} // namespace efgrabber
