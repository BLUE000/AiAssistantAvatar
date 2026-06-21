#include "duckduckgo_search_provider.h"
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QDebug>

DuckDuckGoSearchProvider::DuckDuckGoSearchProvider(QObject *parent)
    : ISearchProvider(parent)
{
    m_networkManager = new QNetworkAccessManager(this);
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &DuckDuckGoSearchProvider::on_replyFinished);
}

void DuckDuckGoSearchProvider::search(const QString &query) {
    QUrl url("https://html.duckduckgo.com/html/");
    QUrlQuery urlQuery;
    urlQuery.addQueryItem("q", query);
    url.setQuery(urlQuery);

    QNetworkRequest request(url);
    // 最新のUser-Agentに変更し、Sec-Fetchヘッダーなどを追加してブラウザ挙動を精緻に偽装
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36");
    request.setRawHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7");
    request.setRawHeader("Accept-Language", "ja,en-US;q=0.9,en;q=0.8");
    request.setRawHeader("Sec-Fetch-Dest", "document");
    request.setRawHeader("Sec-Fetch-Mode", "navigate");
    request.setRawHeader("Sec-Fetch-Site", "none");
    request.setRawHeader("Sec-Fetch-User", "?1");
    request.setRawHeader("Upgrade-Insecure-Requests", "1");

    qDebug() << "DuckDuckGoSearchProvider: Fetching HTML for query:" << query;
    m_networkManager->get(request);
}

static QString cleanHtml(QString text) {
    // HTMLタグの除去
    text.remove(QRegularExpression("<[^>]*>"));
    // 主要なHTMLエンティティのデコード
    text.replace("&amp;", "&");
    text.replace("&quot;", "\"");
    text.replace("&#x27;", "'");
    text.replace("&lt;", "<");
    text.replace("&gt;", ">");
    text.replace("&#x2F;", "/");
    text.replace("&nbsp;", " ");
    text.replace("\r", "");
    text.replace("\n", " ");
    return text.trimmed();
}

void DuckDuckGoSearchProvider::on_replyFinished(QNetworkReply *reply) {
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        QString errorMsg = QString("DuckDuckGo Network Error: %1 (%2)")
                            .arg(reply->errorString())
                            .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
        qWarning() << errorMsg;
        emit searchFinished(errorMsg, false);
        return;
    }

    QString html = QString::fromUtf8(reply->readAll());

    // 各検索結果ブロックの抽出
    QRegularExpression bodyRegex("<div class=\"[^\"]*result__body[^\"]*\">([\\s\\S]*?)(?=<div class=\"result|<div class=\"results_links|<!--|$)");
    QRegularExpression titleRegex("<a class=\"[^\"]*result__a[^\"]*\"[^>]*href=\"([^\"]*)\"[^>]*>([\\s\\S]*?)</a>");
    QRegularExpression snippetRegex("<(?:a|span|div) class=\"[^\"]*result__snippet[^\"]*\"[^>]*>([\\s\\S]*?)</(?:a|span|div)>");

    QStringList formattedResults;
    int index = 1;

    auto bodyIt = bodyRegex.globalMatch(html);
    while (bodyIt.hasNext() && index <= 3) {
        QRegularExpressionMatch bodyMatch = bodyIt.next();
        QString bodyHtml = bodyMatch.captured(1);

        QRegularExpressionMatch titleMatch = titleRegex.match(bodyHtml);
        QRegularExpressionMatch snippetMatch = snippetRegex.match(bodyHtml);

        if (titleMatch.hasMatch()) {
            QString rawUrl = titleMatch.captured(1);
            QString title = cleanHtml(titleMatch.captured(2));
            QString snippet = snippetMatch.hasMatch() ? cleanHtml(snippetMatch.captured(1)) : "";

            // URLのデコード
            QUrl resolvedUrl(rawUrl);
            if (resolvedUrl.path() == "/l/") {
                QUrlQuery query(resolvedUrl.query());
                if (query.hasQueryItem("uddg")) {
                    rawUrl = query.queryItemValue("uddg", QUrl::FullyDecoded);
                }
            }

            formattedResults.append(QString("[%1] %2 (%3): %4")
                                    .arg(index)
                                    .arg(title)
                                    .arg(rawUrl)
                                    .arg(snippet));
            index++;
        }
    }

    if (formattedResults.isEmpty()) {
        if (html.contains("Forbidden") || html.contains("rate limit") || html.contains("robot")) {
            qWarning() << "DuckDuckGoSearchProvider: Blocked or Rate Limited.";
            emit searchFinished("DuckDuckGo search was blocked or rate-limited.", false);
        } else {
            qWarning() << "DuckDuckGoSearchProvider: No results parsed from HTML. Length:" << html.length();
            emit searchFinished("No search results could be parsed from DuckDuckGo HTML.", false);
        }
    } else {
        emit searchFinished(formattedResults.join("\n"), true);
    }
}
