#pragma once
#include "iai_client.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonArray>

class SearchManager;

class MistralAIClient : public IAIClient {
    Q_OBJECT
private:
    QNetworkAccessManager *m_networkManager;
    QString m_apiKey;
    SearchManager *m_searchManager;

    // Function Calling 状態管理用
    QString m_pendingPrompt;
    QJsonArray m_pendingMessages;
    QString m_activeToolCallId;
    bool m_isToolCalling;

public:
    explicit MistralAIClient(QObject *parent = nullptr);
    ~MistralAIClient() override;
    void sendRequest(const QString &prompt, const QList<QPair<QString, QString>> &history = {}, const QString &sessionContext = QString()) override;
    void setApiKey(const QString &apiKey) override;
    void setTavilyApiKey(const QString &tavilyKey);

private slots:
    void on_networkReplyFinished(QNetworkReply *reply);
    void on_searchFinished(const QString &resultText, bool success);
};
