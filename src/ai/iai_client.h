#pragma once
#include <QObject>
#include <QList>
#include <QPair>
#include <QString>
#include "provider_status.h"

class IAIClient : public QObject {
    Q_OBJECT
public:
    explicit IAIClient(QObject *parent = nullptr);
    virtual ~IAIClient();
    virtual void sendRequest(const QString &prompt, const QList<QPair<QString, QString>> &history = {}, const QString &sessionContext = QString(), const QString &systemInstruction = QString()) = 0;
    virtual void setApiKey(const QString &apiKey) = 0;
    virtual void setTavilyApiKey(const QString &tavilyKey) { Q_UNUSED(tavilyKey); }
    virtual QString clientId() const = 0;
    virtual ProviderStatus defaultStatus() const = 0;
    virtual QString currentModelName() const { return QString(); }

    static QString cleanHistoryPrompt(const QString &text) {
        QString cleaned = text;
        if (cleaned.startsWith("[Direct] ")) {
            cleaned = cleaned.mid(9);
        } else if (cleaned.startsWith("[Twitch] ") || cleaned.startsWith("[Discord] ")) {
            int colonIdx = cleaned.indexOf(": ");
            if (colonIdx != -1) {
                cleaned = cleaned.mid(colonIdx + 2);
            }
        }
        return cleaned.trimmed();
    }

signals:
    // AI応答完了通知（成功フラグ・HTTPステータスコード付き）
    // httpCode: HTTP ステータスコード（非 HTTP エラーや成功時は 0 または 200）
    void requestFinished(const QString &responseText, bool success, int httpCode);
};
