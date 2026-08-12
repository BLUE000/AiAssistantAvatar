#pragma once
#include <QObject>
#include "app_event.h"

class CoreModule : public QObject {
    Q_OBJECT
public:
    explicit CoreModule(QObject *parent = nullptr);
    ~CoreModule();

signals:
    // UIスレッドへ非同期で通知するシグナル
    void notifyEventToUI(const AppEvent &event);
    
    // サブモジュールへ要求を送るシグナル
    void requestTwitchStart();
    void requestSTTStart();
    void requestSTTStop();
    void requestAI(const QString &prompt, const QString &user = "", const QString &source = "UI");
    void requestSessionReset(bool isManual); // セッションリセット要求シグナル
    void requestSessionImport(const QString &filePath);
    void requestSessionExport(const QString &encPath, const QString &txtPath);
    void settingsUpdated(); // 設定更新通知シグナル
    void requestTwitchReauth(); // Twitch再認可要求シグナル
    void requestDiscordSend(const QString &channelId, const QString &text); // Discordメッセージ送信要求
    void requestTwitchSend(const QString &channel, const QString &text);   // Twitchメッセージ送信要求
    void requestDeleteKnowledge(const QString &id);
    void requestKnowledgeMetadata();
    void requestTwitchConnect();   // /twitch connect コマンド → TwitchReader へ挨拶付き再接続
    void requestDiscordConnect();  // /discord connect コマンド → DiscordReader へ挨拶付き再接続
    void requestTwitchRaid(const QString &username);
    void requestShoutoutSuccess(const QString &username);

public slots:
    // 他モジュール（Twitch, STT, AI）からのイベントを受け取るスロット
    void on_notify_events(const AppEvent &event);

    // UIからの直接命令を受け取るスロット
    void on_startSTTRequested();
    void on_stopSTTRequested();
    void on_directInputSubmitted(const QString &text);

    void on_resetSessionRequested(); // UIからのセッションリセット要求を受け取るスロット
    void on_importSessionRequested(const QString &filePath);
    void on_exportSessionRequested(const QString &encPath, const QString &txtPath);
    void on_settingsUpdated(); // 設定更新を受け取るスロット
    void on_twitchReauthRequested(); // Twitch再認可要求を受け取るスロット
    void on_deleteKnowledgeRequested(const QString &id);
    void on_requestKnowledgeMetadata();

    // 500文字自動分割ヘルパー関数（単体テスト可能）
    static QStringList splitTextForComment(const QString &text, int maxLen = 500);

private slots:
    void processCommentQueue();

private:
    struct CommentQueueItem {
        enum Target { Twitch, Discord } target;
        QString destination; // channel or channelId
        QString text;
    };
    QList<CommentQueueItem> m_commentQueue;
    class QTimer *m_commentTimer = nullptr;
    int m_slowModeIntervalMs = 1500; // スローモード対応送信間隔 (1.5秒)

    void enqueueCommentSend(CommentQueueItem::Target target, const QString &destination, const QString &fullText);
};

