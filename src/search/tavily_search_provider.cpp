#include "tavily_search_provider.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QDebug>

TavilySearchProvider::TavilySearchProvider(const QString &apiKey, QObject *parent)
    : ISearchProvider(parent), m_apiKey(apiKey)
{
    m_networkManager = new QNetworkAccessManager(this);
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &TavilySearchProvider::on_replyFinished);
}

void TavilySearchProvider::search(const QString &query) {
    if (m_apiKey.isEmpty()) {
        emit searchFinished("Tavily API key is empty.", false);
        return;
    }

    QUrl url("https://api.tavily.com/search");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject requestBody;
    requestBody["api_key"] = m_apiKey;
    requestBody["query"] = query;
    requestBody["max_results"] = 3;

    QJsonDocument doc(requestBody);
    QByteArray postData = doc.toJson();

    qDebug() << "TavilySearchProvider: Sending search query:" << query;
    m_networkManager->post(request, postData);
}

void TavilySearchProvider::on_replyFinished(QNetworkReply *reply) {
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        QString errorMsg = QString("Tavily Network Error: %1 (%2)")
                            .arg(reply->errorString())
                            .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
        qWarning() << errorMsg;
        emit searchFinished(errorMsg, false);
        return;
    }

    QByteArray responseData = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    if (doc.isNull() || !doc.isObject()) {
        emit searchFinished("Tavily response parsing failed.", false);
        return;
    }

    QJsonObject obj = doc.object();
    if (obj.contains("results") && obj["results"].isArray()) {
        QJsonArray results = obj["results"].toArray();
        QStringList formattedResults;
        int index = 1;
        for (const auto &val : results) {
            QJsonObject resObj = val.toObject();
            QString title = resObj["title"].toString();
            QString url = resObj["url"].toString();
            QString content = resObj["content"].toString();
            formattedResults.append(QString("[%1] %2 (%3): %4")
                                    .arg(index)
                                    .arg(title)
                                    .arg(url)
                                    .arg(content));
            index++;
        }
        if (formattedResults.isEmpty()) {
            emit searchFinished("No search results found.", true);
        } else {
            emit searchFinished(formattedResults.join("\n"), true);
        }
        return;
    }

    emit searchFinished("No valid search results structure in Tavily response.", false);
}
