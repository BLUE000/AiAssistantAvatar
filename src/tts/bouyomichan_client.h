#pragma once
#include <QObject>
#include <QString>
#include <QNetworkAccessManager>

class BouyomiChanClient : public QObject {
    Q_OBJECT
public:
    explicit BouyomiChanClient(QObject *parent = nullptr);
    void sendText(const QString &text, bool enabled, const QString &baseUrl);

private:
    QNetworkAccessManager m_networkManager;
};
