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
    QString getCookieString(const QString& domain = ".justice.gov") const;

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
