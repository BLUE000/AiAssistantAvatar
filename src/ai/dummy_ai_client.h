#pragma once
#include "iai_client.h"
#include "provider_status.h"
#include <QTimer>

class DummyAIClient : public IAIClient {
    Q_OBJECT
private:
    QTimer *m_dummyTimer;
    QString m_lastPrompt;
    int m_timerIntervalMs = 0;

public:
    explicit DummyAIClient(QObject *parent = nullptr);
    ~DummyAIClient() override;
    void sendRequest(const QString &prompt, const QList<QPair<QString, QString>> &history = {}, const QString &sessionContext = QString(), const QString &systemInstruction = QString()) override;
    void setApiKey(const QString &apiKey) override;
    void stopTimer();
    void setTimerIntervalMs(int ms) { m_timerIntervalMs = ms; }
    int timerIntervalMs() const { return m_timerIntervalMs; }
    QString clientId() const override { return QStringLiteral("dummy"); }
    ProviderStatus defaultStatus() const override;
    QString currentModelName() const override { return QStringLiteral("dummy-model"); }
};
