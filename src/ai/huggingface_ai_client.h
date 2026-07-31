#pragma once
#include "iai_client.h"
#include "provider_status.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonArray>

class SearchManager;

class HuggingFaceAIClient : public IAIClient {
    Q_OBJECT
private:
    QNetworkAccessManager *m_networkManager;
    QString m_apiKey;
    QString m_model;           // デフォルト: "meta-llama/Llama-3.1-8B-Instruct"
    SearchManager *m_searchManager;

    // Function Calling 状態管理用
    QString m_pendingPrompt;
    QJsonArray m_pendingMessages;
    QString m_activeToolCallId;
    bool m_isToolCalling;
    QJsonArray m_toolsArray;
    bool m_isFetchingModels = false;

    void fetchAvailableModels();

public:
    explicit HuggingFaceAIClient(QObject *parent = nullptr);
    ~HuggingFaceAIClient() override;

    void sendRequest(const QString &prompt,
                     const QList<QPair<QString,QString>> &history = {},
                     const QString &sessionContext = QString(),
                     const QString &systemInstruction = QString()) override;
    void setApiKey(const QString &apiKey) override;
    QString apiKey() const override { return m_apiKey; }
    void setModel(const QString &model);
    void setTavilyApiKey(const QString &tavilyKey) override;
    QString clientId() const override { return QStringLiteral("huggingface"); }
    ProviderStatus defaultStatus() const override;
    QString currentModelName() const override { return m_model; }

private slots:
    void on_networkReplyFinished(QNetworkReply *reply);
    void on_searchFinished(const QString &resultText, bool success);
};
