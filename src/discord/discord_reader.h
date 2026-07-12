#pragma once
#include <QObject>
#include <QWebSocket>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QJsonObject>
#include "../app_event.h"

class DiscordReader : public QObject {
    Q_OBJECT
private:
    bool m_isRunning = false;
    bool m_enabled = false;
    QString m_botToken;
    QString m_channelId;
    QWebSocket *m_webSocket = nullptr;
    QNetworkAccessManager *m_networkManager = nullptr;
    QTimer *m_heartbeatTimer = nullptr;
    int m_lastSequence = 0;
    bool m_hasAck = true;
    QString m_botUserId; // ボット自身のID（無限ループ防止用）
    QString m_wakeWord;
    QString m_wakeWordMode;
    bool m_nameReactionEnabled = true;
    QString m_avatarName;
    bool m_shouldGreet = false;       // READY 受信後に挨拶するかフラグ
    QString m_lastGreetedChannelId;   // 直前に挨拶したチャンネルID（二重挨拶防止）
    bool m_greetingEnabled = false;   // local_settings.json の greeting_enabled が true の時のみ ON
    QString m_configPath;

    void loadSettings();
    void connectToDiscord();
    void sendHeartbeat();
    void identify();
    void parseGatewayMessage(const QString &message);
    void sendGreeting();  // READY確認後に挨拶を発火

public:
    explicit DiscordReader(QObject *parent = nullptr);
    ~DiscordReader();
    bool isGreetingEnabled() const { return m_greetingEnabled; }
    void setConfigPath(const QString &path) { m_configPath = path; }

signals:
    void notifyEvent(const AppEvent &event);

public slots:
    void on_startReading();
    void on_stopReading();
    void on_settingsUpdated();
    void on_discordConnectRequested(); // /discord connect コマンドで呼ばれる（挨拶付き再接続）
    void on_requestDiscordSend(const QString &channelId, const QString &text);

private slots:
    void onWebSocketConnected();
    void onWebSocketDisconnected();
    void onTextMessageReceived(const QString &message);
    void onReplyFinished(QNetworkReply *reply);
};
