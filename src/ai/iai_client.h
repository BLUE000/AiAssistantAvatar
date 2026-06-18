#pragma once
#include <QObject>

class IAIClient : public QObject {
    Q_OBJECT
public:
    explicit IAIClient(QObject *parent = nullptr);
    virtual ~IAIClient();
    virtual void sendRequest(const QString &prompt) = 0;
    virtual void setApiKey(const QString &apiKey) = 0;

signals:
    // AI応答完了通知（成功フラグ付き）
    void requestFinished(const QString &responseText, bool success);
};
