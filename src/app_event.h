#pragma once
#include <QString>
#include <QVariantMap>

enum class EventType {
    TwitchCommentReceived,  // 対象のコメント受信
    VoiceInputStarted,       // 音声認識開始
    VoiceInputCompleted,     // 音声認識完了 (テキスト有り)
    DirectInputSubmitted,    // キーボード直接入力
    AIRequestSent,          // AIへ送信開始
    AIResponseReceived,     // AIからの回答受信
    ErrorOccurred,          // エラー発生
    ChatHistoryReceived     // 会話履歴データ受信
};

struct AppEvent {
    EventType type;
    QString text;
    QString source;
    QVariantMap extraData;
};

// Qt の QueuedConnection でシグナル・スロットの引数として渡せるようにメタタイプ登録する準備
#include <QMetaType>
Q_DECLARE_METATYPE(AppEvent)
