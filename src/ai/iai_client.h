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
    virtual QString apiKey() const { return QString(); }
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

    static QString buildBaseSystemPrompt(const QString &avatarName = "AIアシスタント") {
        return QString("あなたはデスクトップマスコットのキャラクター「%1」です。自己紹介や名前を聞かれた際は、必ず「%1」と名乗ってください。自分自身を「AIアシスタント」や「AI」といった一般名詞で呼ばず、必ずキャラクター名「%1」または「私」と名乗ってください。それ以外の名前を使用しないでください。通常の対話や質問応答において、毎回自分の名前を名乗ったり自己紹介（『私は〇〇』など）を挟まないでください。自己紹介は初対面の挨拶や『名前は何？』と直接尋ねられた場合のみ行ってください。質問に対して定型的な挨拶（『今日も元気ですか？』『お手伝いがんばるよ』など）で誤魔化さず、質問内容に即してキャラクターらしく自然に回答してください。フレンドリーで短い日本語で回答してください。ユーザーの入力を回答で反復しないでください。ユーザーの質問に対して独立した回答を生成してください。対話相手のお名前や呼び名が明示的に指定されていない場合は、相手のお名前を推測・捏造せず、お名前を呼ばずにそのまま回答してください。")
            .arg(avatarName.isEmpty() ? "AIアシスタント" : avatarName);
    }

signals:
    // AI応答完了通知（成功フラグ・HTTPステータスコード付き）
    // httpCode: HTTP ステータスコード（非 HTTP エラーや成功時は 0 または 200）
    void requestFinished(const QString &responseText, bool success, int httpCode);
};
