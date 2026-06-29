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

    void loadSettings();
    void connectToDiscord();
    void sendHeartbeat();
    void identify();
    void parseGatewayMessage(const QString &message);

public:
    explicit DiscordReader(QObject *parent = nullptr);
    ~DiscordReader();

signals:
    void notifyEvent(const AppEvent &event);

public slots:
    void on_startReading();
    void on_stopReading();
    void on_settingsUpdated();
    void on_requestDiscordSend(const QString &channelId, const QString &text);

private slots:
    void onWebSocketConnected();
    void onWebSocketDisconnected();
    void onTextMessageReceived(const QString &message);
    void onReplyFinished(QNetworkReply *reply);
};
