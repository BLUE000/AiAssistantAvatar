#pragma once
#include <QObject>
#include "iai_client.h"
#include "../app_event.h"

class AIClientManager : public QObject {
    Q_OBJECT
private:
    IAIClient *m_currentClient = nullptr;
    QString m_apiKey;
    QString m_provider; // "mistral" or "dummy"
    QString m_transCipherKey; // 難読化用の秘密鍵

    void loadCredentials();
    void saveObfuscatedLog(const QString &logText); // TransCipherを用いたログ難読化保存

public:
    explicit AIClientManager(QObject *parent = nullptr);
    ~AIClientManager();
    void setAIProvider(const QString &provider);

signals:
    void notifyEvent(const AppEvent &event);

public slots:
    void on_requestAI(const QString &prompt);
    void on_clientRequestFinished(const QString &responseText, bool success);
};
