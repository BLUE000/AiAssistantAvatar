#pragma once
#include <QObject>
#include <QWebSocket>
#include <QTcpServer>
#include "../app_event.h"

class TwitchReader : public QObject {
    Q_OBJECT
private:
    bool m_isRunning = false;
    QString m_channel;
    QString m_oauthToken;
    QString m_clientId;
    QString m_wakeWord; // UIで設定変更可能
    QString m_wakeWordMode; // "contains" または "prefix" / "command"
    int m_authPort = 48080;

    QWebSocket *m_webSocket = nullptr;
    QTcpServer *m_authServer = nullptr;
    QString m_configPath;

    void loadSettings();
    void saveTokenToSettings(const QString &token);
    void saveOAuthDataToSettings(const QString &token, const QString &channel);
    void fetchChannelName(const QString &token);
    void startOAuthServer();
    void connectToTwitch();

public:
    explicit TwitchReader(QObject *parent = nullptr);
    ~TwitchReader();

    void setSettings(const QString &channel, const QString &token, const QString &clientId, const QString &wakeWord);
    void setWakeWordMode(const QString &mode) { m_wakeWordMode = mode.trimmed().toLower(); }

signals:
    void notifyEvent(const AppEvent &event);

public slots:
    void on_startReading();
    void on_stopReading();
    void on_settingsUpdated();
    void on_twitchReauthRequested();
    
    // テスト用の擬似コメント注入用スロット
    void injectTestComment(const QString &user, const QString &message);

private slots:
    // QTcpServer用の接続ハンドラ
    void handleNewConnection();
    
    // QWebSocket用の接続イベントハンドラ
    void onWebSocketConnected();
    void onWebSocketDisconnected();
    void onTextMessageReceived(const QString &message);
};

