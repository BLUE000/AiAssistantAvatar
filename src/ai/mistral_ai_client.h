#pragma once
#include "iai_client.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>

class MistralAIClient : public IAIClient {
    Q_OBJECT
private:
    QNetworkAccessManager *m_networkManager;
    QString m_apiKey;

public:
    explicit MistralAIClient(QObject *parent = nullptr);
    ~MistralAIClient() override;
    void sendRequest(const QString &prompt, const QList<QPair<QString, QString>> &history = {}, const QString &sessionContext = QString()) override;
    void setApiKey(const QString &apiKey) override;

private slots:
    void on_networkReplyFinished(QNetworkReply *reply);
};
