#pragma once
#include <QObject>
#include <QList>
#include <QPair>
#include <QString>

class IAIClient : public QObject {
    Q_OBJECT
public:
    explicit IAIClient(QObject *parent = nullptr);
    virtual ~IAIClient();
    virtual void sendRequest(const QString &prompt, const QList<QPair<QString, QString>> &history = {}, const QString &sessionContext = QString()) = 0;
    virtual void setApiKey(const QString &apiKey) = 0;

signals:
    // AI応答完了通知（成功フラグ付き）
    void requestFinished(const QString &responseText, bool success);
};
