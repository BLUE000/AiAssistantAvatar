#pragma once
#include <QObject>
#include <QWebSocket>
#include <QTcpServer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QDateTime>
#include "../app_event.h"

class TwitchReader : public QObject {
    Q_OBJECT
    friend class TwitchReaderTest;
private:
    bool m_isRunning = false;
    QString m_channel;
    QString m_oauthToken;
    QString m_clientId;
    QString m_botName;
    QString m_wakeWord; // UIで設定変更可能
    QString m_wakeWordMode; // "contains" または "prefix" / "command"
    QString m_avatarName;
    bool m_nameReactionEnabled = true;
    int m_authPort = 48080;

    QWebSocket *m_webSocket = nullptr;
    QTcpServer *m_authServer = nullptr;
    QNetworkAccessManager *m_networkManager = nullptr;
    QString m_configPath;
    QTimer *m_reconnectTimer = nullptr; // connectToTwitch() の debounce 用
    QTimer *m_watchdogTimer = nullptr;  // サイレント切断探知用 Watchdog タイマー (60秒周期)
    QDateTime m_lastDataReceivedTime;   // 最後に Twitch からデータを受信した時刻
    bool m_shouldGreet = false;          // 挨拶すべきチャンネル切替より後の初回 JOIN のみ true
    QString m_lastGreetedChannel;        // 直前に挨拶したチャンネル（二重挨拶防止）
    bool m_greetingEnabled = false;      // local_settings.json の greeting_enabled が true の時のみ ON

    void loadSettings();
    void saveTokenToSettings(const QString &accessToken);
    void saveOAuthDataToSettings(const QString &accessToken, const QString &channel);
    void fetchChannelName(const QString &token);
    void startOAuthServer();
    void connectToTwitch();       // debounce エントリ（外部から呼ぶ）
    void doConnectToTwitch();     // 実際の接続処理（タイマーから呼ばれる）
    void sendGreeting();          // JOIN確認後に挨拶を発火
protected:
    void checkWatchdog();         // サイレント切断監視用タイマーコールバック
    void onTextMessageReceived(const QString &message);

public:
    explicit TwitchReader(QObject *parent = nullptr);
    ~TwitchReader();

    void setSettings(const QString &channel, const QString &token, const QString &clientId, const QString &wakeWord, const QString &avatarName = "AIアシスタント", bool nameReactionEnabled = true);
    void setWakeWordMode(const QString &mode) { m_wakeWordMode = mode.trimmed().toLower(); }
    bool isGreetingEnabled() const { return m_greetingEnabled; }
    void setConfigPath(const QString &path) { m_configPath = path; }

signals:
    void notifyEvent(const AppEvent &event);

public slots:
    void on_startReading();
    void on_stopReading();
    void on_settingsUpdated();
    void on_twitchReauthRequested();
    void on_twitchConnectRequested(); // /twitch connect コマンドで呼ばれる（挨拶付き再接続）
    
    // テスト用の擬似コメント注入用スロット
    void injectTestComment(const QString &user, const QString &message);

    // Twitchチャットへのメッセージ送信スロット
    void on_requestTwitchSend(const QString &channel, const QString &text);

private slots:
    // QTcpServer用の接続ハンドラ
    void handleNewConnection();
    
    // QWebSocket用の接続イベントハンドラ
    void onWebSocketConnected();
    void onWebSocketDisconnected();
};

