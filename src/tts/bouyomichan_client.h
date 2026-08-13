#pragma once
#include <QObject>
#include <QString>
#include <QNetworkAccessManager>

class BouyomiChanClient : public QObject {
    Q_OBJECT
public:
    explicit BouyomiChanClient(QObject *parent = nullptr);
    void sendText(const QString &text, bool enabled, const QString &baseUrl);

    // 単体テスト検証・ソケット送信用パケット生成関数
    static QByteArray createTcpPacket(const QString &text);

private:
    void sendSocketText(const QString &host, int port, const QString &text);

    QNetworkAccessManager m_networkManager;
};

