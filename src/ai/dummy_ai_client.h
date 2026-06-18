#pragma once
#include "iai_client.h"
#include <QTimer>

class DummyAIClient : public IAIClient {
    Q_OBJECT
private:
    QTimer *m_dummyTimer;
    QString m_lastPrompt;

public:
    explicit DummyAIClient(QObject *parent = nullptr);
    ~DummyAIClient() override;
    void sendRequest(const QString &prompt) override;
    void setApiKey(const QString &apiKey) override;
};
