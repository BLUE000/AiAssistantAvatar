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
    SettingsUpdated         // 設定更新
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

namespace ConfigDefaults {
    inline const int WEBSOCKET_PORT = 58081;
    inline const int TWITCH_PORT = 48080;
    inline const QString WAKE_WORD = QStringLiteral("アバターさん");
    inline const QString WAKE_WORD_MODE = QStringLiteral("contains");
    inline const QString AI_PROVIDER = QStringLiteral("dummy");
}
