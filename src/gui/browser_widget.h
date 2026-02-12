/*
 * browser_widget.h - Widget embedding QWebEngineView for browser scraping
 * Copyright © 2026 Kirn Gill II <segin2005@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef EFGRABBER_BROWSER_WIDGET_H
#define EFGRABBER_BROWSER_WIDGET_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QString>
#include <QMap>
#include <functional>
#include "efgrabber/common.h"

#ifdef HAVE_WEBENGINE
#include <QWebEngineView>
#include <QWebEngineCookieStore>
#include <QWebEngineProfile>
#include <QNetworkCookie>
#endif

namespace efgrabber {

class BrowserWidget : public QWidget {
    Q_OBJECT

public:
    explicit BrowserWidget(QWidget* parent = nullptr);
    ~BrowserWidget() override = default;

    // Navigate to URL
    void navigateTo(const QString& url);

    // Get current URL
    QString currentUrl() const;

    // Get all cookies as a curl-compatible string (name=value; name2=value2)
    QString getCookieString(const QString& domain = QString::fromStdString(TARGET_DOMAIN)) const;

    // Get cookies in Netscape format for writing to file
    QString getCookiesNetscapeFormat() const;

    // Check if we have cookies for a domain
    bool hasCookiesFor(const QString& domain) const;

#ifdef HAVE_WEBENGINE
    QWebEngineProfile* profile() const;
#endif

signals:
    void cookiesChanged();
    void urlChanged(const QString& url);
    void pdfLinkFound(const QString& fileId, const QString& url);
    void pageHtmlReady(const QString& url, const QString& html);

public slots:
    void goToDataSet(int dataSet);
    void fetchPageForScraping(const QString& url);  // Fetch a page and emit HTML

private slots:
    void onGoClicked();
    void onUrlChanged(const QUrl& url);
    void onLoadFinished(bool ok);
#ifdef HAVE_WEBENGINE
    void onCookieAdded(const QNetworkCookie& cookie);
    void onCookieRemoved(const QNetworkCookie& cookie);
#endif

private:
    void setupUi();
    void scanForPdfLinks(const QString& html);

    QVBoxLayout* mainLayout_;
    QHBoxLayout* navLayout_;
    QLineEdit* urlEdit_;
    QPushButton* goButton_;
    QPushButton* backButton_;
    QPushButton* forwardButton_;
    QLabel* statusLabel_;

#ifdef HAVE_WEBENGINE
    QWebEngineView* webView_;
    // Store cookies: key = "domain:name", value = cookie
    QMap<QString, QNetworkCookie> cookies_;
#else
    QLabel* placeholderLabel_;
#endif
};

} // namespace efgrabber

#endif // EFGRABBER_BROWSER_WIDGET_H
