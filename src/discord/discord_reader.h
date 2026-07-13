#pragma once
#include <QObject>
#include <QWebSocket>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QJsonObject>
#include <QSet>
#include "../app_event.h"

class DiscordReader : public QObject {
    Q_OBJECT
private:
    bool m_isRunning = false;
    bool m_enabled = false;
    QString m_botToken;

    struct ChannelConfig {
        QString id;
        bool greetingEnabled = false;
    };
    QList<ChannelConfig> m_channels;

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
    QSet<QString> m_greetedChannels;  // 挨拶送信済みのチャンネルID一覧
    QString m_configPath;

    void loadSettings();
    void connectToDiscord();
    void sendHeartbeat();
    void identify();
    void parseGatewayMessage(const QString &message);
    void sendGreetings();                     // 全対象チャンネルへ挨拶を送信
    void sendChannelGreeting(const QString &channelId); // 指定チャンネルへ挨拶を送信

public:
    explicit DiscordReader(QObject *parent = nullptr);
    ~DiscordReader();
    bool isGreetingEnabled() const {
        for (const auto &ch : m_channels) {
            if (ch.greetingEnabled) return true;
        }
        return false;
    }
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
