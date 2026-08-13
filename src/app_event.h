#pragma once
#include <QString>
#include <QVariantMap>

enum class EventType {
    TwitchCommentReceived,    // 対象のコメント受信
    DiscordMessageReceived,   // Discordメッセージ受信
    VoiceInputStarted,        // 音声認識開始
    VoiceInputCompleted,      // 音声認識完了 (テキスト有り)
    DirectInputSubmitted,     // キーボード直接入力
    AIRequestSent,            // AIへ送信開始
    AIResponseReceived,       // AIからの回答受信
    ErrorOccurred,            // エラー発生
    SettingsUpdated,          // 設定更新
    TwitchConnectRequested,   // /twitch connect コマンド（挨拶付き再接続）
    DiscordConnectRequested,  // /discord connect コマンド（挨拶付き再接続）
    TwitchRaidReceived,       // Twitchレイド受信
    ShoutoutCooldownUpdated,  // シャウトアウトクールタイム残り秒数更新
    ShoutoutQueueUpdated,     // シャウトアウト送信待機中キューリスト更新
    ShoutoutSuccessReceived   // /shoutout コマンド成功通知受信
};

enum class ReplyTarget : uint32_t {
    None        = 0,
    UI          = 1 << 0,  // 本体アプリUI表示
    WebText     = 1 << 1,  // /ui_text 専用Webテキスト
    OBSOverlay  = 1 << 2,  // avatar_obs.html アバター画面
    TwitchChat  = 1 << 3,  // Twitchチャット返信
    DiscordChat = 1 << 4,  // Discordメッセージ返信
    TTSVoice    = 1 << 5   // 音声読み上げ Engine
};

struct AppEvent {
    EventType type;
    QString text;
    QString source;
    uint32_t replyTarget = 0;
    QVariantMap extraData;
};

// Qt の QueuedConnection でシグナル・スロットの引数として渡せるようにメタタイプ登録する準備
#include <QMetaType>
Q_DECLARE_METATYPE(AppEvent)

namespace ConfigDefaults {
    inline const int WEBSOCKET_PORT = 58081;
    inline const int TWITCH_PORT = 48080;
    inline const QString WAKE_WORD = QStringLiteral("AIアシスタント");
    inline const QString WAKE_WORD_MODE = QStringLiteral("contains");
    inline const QString AI_PROVIDER = QStringLiteral("dummy");
    inline const QString DEFAULT_HUGGINGFACE_MODEL = QStringLiteral("meta-llama/Llama-3.1-8B-Instruct");
    inline const QString DEFAULT_OPENROUTER_MODEL = QStringLiteral("google/gemma-4-31b-it:free");
    inline const QString DEFAULT_SAKURA_MODEL = QStringLiteral("llm-jp-3.1-8x13b-instruct4");
    inline const QString BOUYOMI_URL = QStringLiteral("http://localhost:50080/talk");
}

