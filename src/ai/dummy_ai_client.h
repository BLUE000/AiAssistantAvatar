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
    void sendRequest(const QString &prompt, const QList<QPair<QString, QString>> &history = {}, const QString &sessionContext = QString(), const QString &systemInstruction = QString()) override;
    void setApiKey(const QString &apiKey) override;
};
