#include "bouyomichan_client.h"
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QDebug>

BouyomiChanClient::BouyomiChanClient(QObject *parent)
    : QObject(parent) {
}

void BouyomiChanClient::sendText(const QString &text, bool enabled, const QString &baseUrl) {
    if (!enabled || text.isEmpty() || baseUrl.isEmpty()) {
        return;
    }

    QString urlStr = baseUrl;
    if (urlStr.contains("?")) {
        urlStr += "&text=" + QString::fromUtf8(QUrl::toPercentEncoding(text));
    } else {
        urlStr += "?text=" + QString::fromUtf8(QUrl::toPercentEncoding(text));
    }

    QUrl requestUrl(urlStr);
    QNetworkRequest request(requestUrl);

    qDebug() << "BouyomiChanClient: Sending GET request to:" << requestUrl.toString();
    QNetworkReply *reply = m_networkManager.get(request);

    connect(reply, &QNetworkReply::finished, reply, [reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "BouyomiChanClient error:" << reply->errorString();
        } else {
            qDebug() << "BouyomiChanClient: Request succeeded.";
        }
        reply->deleteLater();
    });
}
