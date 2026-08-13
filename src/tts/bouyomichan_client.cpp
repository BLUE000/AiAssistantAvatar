#include "bouyomichan_client.h"
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QTcpSocket>
#include <QDataStream>
#include <QDebug>

BouyomiChanClient::BouyomiChanClient(QObject *parent)
    : QObject(parent) {
}

QByteArray BouyomiChanClient::createTcpPacket(const QString &text) {
    QByteArray textBytes = text.toUtf8();
    quint16 command = 0x0001; // 読み上げコマンド
    qint16 speed = -1;       // デフォルト速度 (-1)
    qint16 tone = -1;        // デフォルト音程 (-1)
    qint16 volume = -1;      // デフォルト音量 (-1)
    quint16 voice = 0;       // デフォルト声質 (0)
    quint8 encoding = 0;     // 0: UTF-8
    quint32 length = static_cast<quint32>(textBytes.size());

    QByteArray packet;
    QDataStream stream(&packet, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << command << speed << tone << volume << voice << encoding << length;
    packet.append(textBytes);
    return packet;
}

void BouyomiChanClient::sendSocketText(const QString &host, int port, const QString &text) {
    QTcpSocket *socket = new QTcpSocket(this);
    qDebug() << "BouyomiChanClient: Connecting TCP socket to" << host << ":" << port;

    connect(socket, &QTcpSocket::connected, this, [socket, host, port, text]() {
        QByteArray packet = createTcpPacket(text);
        qint64 bytesWritten = socket->write(packet);
        socket->flush();
        qDebug() << "BouyomiChanClient: Sent TCP binary packet to" << host << ":" << port << "(" << bytesWritten << "bytes written). Text:" << text;
        socket->disconnectFromHost();
    });

    connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);

    connect(socket, &QAbstractSocket::errorOccurred, this, [socket, host, port](QAbstractSocket::SocketError err) {
        qWarning() << "BouyomiChanClient TCP Socket error:" << socket->errorString() << "(code:" << err << ") on" << host << ":" << port;
        socket->deleteLater();
    });

    socket->connectToHost(host, static_cast<quint16>(port));
}

void BouyomiChanClient::sendText(const QString &text, bool enabled, const QString &baseUrl) {
    if (!enabled) {
        qDebug() << "BouyomiChanClient: Integration disabled (enabled=false). Skipping speech.";
        return;
    }
    if (text.isEmpty()) {
        qDebug() << "BouyomiChanClient: Text is empty. Skipping speech.";
        return;
    }
    if (baseUrl.isEmpty()) {
        qWarning() << "BouyomiChanClient: Base URL is empty. Skipping speech.";
        return;
    }

    QUrl parsedUrl(baseUrl.contains("://") ? baseUrl : "http://" + baseUrl);
    int port = parsedUrl.port(-1);
    if (port <= 0 && baseUrl.contains(":50001")) {
        port = 50001;
    }
    QString host = parsedUrl.host();
    if (host.isEmpty()) host = "127.0.0.1";

    // ポート 50001 または TCP 指定の場合は TCP バイナリソケット通信を発動
    if (port == 50001 || baseUrl.startsWith("tcp://", Qt::CaseInsensitive)) {
        if (port <= 0) port = 50001;
        sendSocketText(host, port, text);
        return;
    }

    // 従来の HTTP GET 通信方式
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
            qWarning() << "BouyomiChanClient error:" << reply->errorString() << "HTTP Code:" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        } else {
            qDebug() << "BouyomiChanClient: Request succeeded.";
        }
        reply->deleteLater();
    });
}
