#include "discord_reader.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCoreApplication>
#include <QNetworkRequest>
#include <QDebug>

DiscordReader::DiscordReader(QObject *parent)
    : QObject(parent), m_isRunning(false), m_enabled(false), m_lastSequence(0), m_hasAck(true)
{
    m_webSocket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    m_networkManager = new QNetworkAccessManager(this);
    m_heartbeatTimer = new QTimer(this);

    connect(m_webSocket, &QWebSocket::connected, this, &DiscordReader::onWebSocketConnected);
    connect(m_webSocket, &QWebSocket::disconnected, this, &DiscordReader::onWebSocketDisconnected);
    connect(m_webSocket, &QWebSocket::textMessageReceived, this, &DiscordReader::onTextMessageReceived);
    connect(m_networkManager, &QNetworkAccessManager::finished, this, &DiscordReader::onReplyFinished);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &DiscordReader::sendHeartbeat);

    loadSettings();
}

DiscordReader::~DiscordReader() {
    on_stopReading();
}

void DiscordReader::loadSettings() {
    QString configPath = "local_settings.json";
#ifdef PROJECT_SOURCE_DIR
    if (!QFile::exists(configPath)) {
        configPath = QString(PROJECT_SOURCE_DIR) + "/local_settings.json";
    }
#endif
    if (!QFile::exists(configPath)) {
        configPath = QCoreApplication::applicationDirPath() + "/local_settings.json";
    }

    if (!QFile::exists(configPath)) return;

    QFile file(configPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QByteArray data = file.readAll();
        file.close();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject obj = doc.object();
            m_enabled = obj.value("discord_enabled").toBool(false);
            m_botToken = obj.value("discord_bot_token").toString().trimmed();
            m_channelId = obj.value("discord_channel_id").toString().trimmed();
            m_wakeWord = obj.value("twitch_wakeword").toString().trimmed();
            m_wakeWordMode = obj.value("twitch_wakeword_mode").toString("contains").trimmed();
            m_nameReactionEnabled = obj.value("name_reaction_enabled").toBool(true);
            m_avatarName = obj.value("avatar_name").toString().trimmed();
        }
    }
}

void DiscordReader::on_startReading() {
    loadSettings();
    if (!m_enabled || m_botToken.isEmpty() || m_channelId.isEmpty()) {
        qDebug() << "DiscordReader: Disabled or configuration incomplete.";
        return;
    }

    if (m_isRunning) return;
    m_isRunning = true;
    qDebug() << "DiscordReader: Starting...";
    connectToDiscord();
}

void DiscordReader::on_stopReading() {
    m_isRunning = false;
    m_heartbeatTimer->stop();

    // 不要な非同期切断イベントによる再接続ループの誤動作を防ぐため、一時的にシグナルを切断
    disconnect(m_webSocket, &QWebSocket::disconnected, this, &DiscordReader::onWebSocketDisconnected);

    if (m_webSocket->state() != QAbstractSocket::UnconnectedState) {
        m_webSocket->close();
    }

    // 次回開始時のために再度シグナルを接続
    connect(m_webSocket, &QWebSocket::disconnected, this, &DiscordReader::onWebSocketDisconnected);

    qDebug() << "DiscordReader: Stopped.";
}

void DiscordReader::on_settingsUpdated() {
    qDebug() << "DiscordReader: Settings updated, reloading...";
    QString prevChannelId = m_channelId;
    bool wasRunning = m_isRunning;
    on_stopReading();
    loadSettings();
    // チャンネルIDが変更された場合のみ挨拶フラグを立てる
    if (!m_channelId.isEmpty() && m_channelId != prevChannelId) {
        qDebug() << "DiscordReader: Channel changed from" << prevChannelId << "to" << m_channelId << ". Greeting scheduled.";
        m_shouldGreet = true;
    }
    if (wasRunning && m_enabled) {
        on_startReading();
    }
}

void DiscordReader::on_discordConnectRequested() {
    qDebug() << "DiscordReader: /discord connect requested. Greeting scheduled.";
    m_shouldGreet = true;
    on_stopReading();
    if (m_enabled) {
        on_startReading();
    }
}

void DiscordReader::connectToDiscord() {
    if (!m_isRunning) return;

    // ガード：すでに接続処理中または接続完了している場合は何もしない
    if (m_webSocket->state() != QAbstractSocket::UnconnectedState) {
        qDebug() << "DiscordReader: Already connecting or connected. State:" << m_webSocket->state();
        return;
    }

    // Discord Gateway URL (v10)
    QUrl url("wss://gateway.discord.gg/?v=10&encoding=json");
    qDebug() << "DiscordReader: Connecting to Gateway:" << url.toString();
    m_webSocket->open(url);
}

void DiscordReader::onWebSocketConnected() {
    qDebug() << "DiscordReader: WebSocket connected to Gateway.";
    m_hasAck = true;
}

void DiscordReader::onWebSocketDisconnected() {
    qWarning() << "DiscordReader: WebSocket disconnected. Close Code:" << m_webSocket->closeCode()
               << "Reason:" << m_webSocket->closeReason();
    m_heartbeatTimer->stop();
    
    if (!m_isRunning) return;

    // 自動再接続 (5秒後)
    // 重複したタイマー予約を防ぐため、UnconnectedStateの場合のみ予約を実行
    if (m_webSocket->state() == QAbstractSocket::UnconnectedState) {
        qDebug() << "DiscordReader: Reconnecting in 5 seconds...";
        QTimer::singleShot(5000, this, [this]() {
            if (m_isRunning) {
                connectToDiscord();
            }
        });
    }
}

void DiscordReader::onTextMessageReceived(const QString &message) {
    parseGatewayMessage(message);
}

void DiscordReader::parseGatewayMessage(const QString &message) {
    qDebug() << "DiscordReader: Gateway message received:" << message;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (doc.isNull() || !doc.isObject()) return;

    QJsonObject obj = doc.object();
    int op = obj.value("op").toInt(-1);
    
    // シーケンス番号更新
    if (obj.contains("s") && !obj.value("s").isNull()) {
        m_lastSequence = obj.value("s").toInt();
    }

    switch (op) {
        case 10: { // Hello
            QJsonObject dObj = obj.value("d").toObject();
            int intervalMs = dObj.value("heartbeat_interval").toInt(45000);
            qDebug() << "DiscordReader: Hello received. Heartbeat interval:" << intervalMs << "ms";
            
            // 初回ハートビートはランダムウェイトを推奨されるが、ここでは即座に1回送りタイマーを回す
            sendHeartbeat();
            m_heartbeatTimer->start(intervalMs);

            // 認証 (Identify) 送信
            identify();
            break;
        }
        case 11: { // Heartbeat ACK
            m_hasAck = true;
            break;
        }
        case 9: { // Invalid Session
            qWarning() << "DiscordReader: Invalid Session (op: 9) received! d =" << obj.value("d").toBool();
            m_webSocket->close();
            break;
        }
        case 0: { // Dispatch (READY, MESSAGE_CREATE, etc.)
            QString t = obj.value("t").toString();
            QJsonObject dObj = obj.value("d").toObject();

            if (t == "READY") {
                QJsonObject userObj = dObj.value("user").toObject();
                m_botUserId = userObj.value("id").toString();
                qDebug() << "DiscordReader: Bot is ready. Bot User ID:" << m_botUserId;
                // READY 受信後に挨拶フラグが立っていれば挨拶発火
                if (m_shouldGreet && m_lastGreetedChannelId != m_channelId) {
                    qDebug() << "DiscordReader: READY received. Triggering greeting for channel" << m_channelId;
                    QTimer::singleShot(1000, this, [this]() { sendGreeting(); }); // 1秒待ってから挨拶
                }
            }
            else if (t == "MESSAGE_CREATE") {
                QString channelId = dObj.value("channel_id").toString();
                
                // 対象チャンネルかつ、ボット自身の発言でなければ処理
                if (channelId == m_channelId) {
                    QJsonObject authorObj = dObj.value("author").toObject();
                    QString authorId = authorObj.value("id").toString();
                    
                    if (authorId != m_botUserId && !authorObj.value("bot").toBool(false)) {
                        QString content = dObj.value("content").toString().trimmed();
                        QString username = authorObj.value("username").toString();

                        if (!content.isEmpty()) {
                            bool isMatch = false;
                            QString cleanMessage = content;

                            // 1. まずウェイクワードで判定
                            if (!m_wakeWord.isEmpty()) {
                                if (m_wakeWordMode == "prefix" || m_wakeWordMode == "command") {
                                    if (content.startsWith(m_wakeWord)) {
                                        isMatch = true;
                                        cleanMessage = content.mid(m_wakeWord.length()).trimmed();
                                    }
                                } else {
                                    if (content.contains(m_wakeWord)) {
                                        isMatch = true;
                                        cleanMessage = content;
                                        cleanMessage.replace(m_wakeWord, "");
                                        cleanMessage = cleanMessage.trimmed();
                                    }
                                }
                            }

                            // 2. ウェイクワードでマッチせず、名前で反応が有効な場合、アバター名で判定
                            if (!isMatch && m_nameReactionEnabled && !m_avatarName.isEmpty()) {
                                if (content.contains(m_avatarName)) {
                                    isMatch = true;
                                    cleanMessage = content;
                                    cleanMessage.replace(m_avatarName, "");
                                    cleanMessage = cleanMessage.trimmed();
                                }
                            }

                            if (isMatch) {
                                qDebug() << "DiscordReader: Message matched WakeWord. From" << username << ":" << cleanMessage;
                                
                                AppEvent event;
                                event.type = EventType::DiscordMessageReceived;
                                event.source = "DiscordReader";
                                event.text = cleanMessage;
                                event.extraData["channel_id"] = channelId;
                                event.extraData["username"] = username;
                                event.extraData["user_id"] = authorId;

                                emit notifyEvent(event);
                            }
                        }
                    }
                }
            }
            break;
        }
        default:
            break;
    }
}

void DiscordReader::sendHeartbeat() {
    if (m_webSocket->state() != QAbstractSocket::ConnectedState) return;

    if (!m_hasAck) {
        qWarning() << "DiscordReader: Heartbeat ACK not received since last heartbeat! Reconnecting...";
        m_webSocket->close();
        return;
    }

    m_hasAck = false;
    QJsonObject hObj;
    hObj["op"] = 1;
    if (m_lastSequence > 0) {
        hObj["d"] = m_lastSequence;
    } else {
        hObj["d"] = QJsonValue::Null;
    }

    QJsonDocument doc(hObj);
    m_webSocket->sendTextMessage(doc.toJson(QJsonDocument::Compact));
}

void DiscordReader::identify() {
    if (m_webSocket->state() != QAbstractSocket::ConnectedState) return;

    QJsonObject iObj;
    iObj["op"] = 2; // Identify

    QJsonObject dObj;
    dObj["token"] = m_botToken;
    // intents: GUILD_MESSAGES (512) | MESSAGE_CONTENT (32768) = 33280
    dObj["intents"] = 33280;

    QJsonObject propObj;
    propObj["os"] = "windows";
    propObj["browser"] = "qt";
    propObj["device"] = "qt";
    dObj["properties"] = propObj;

    iObj["d"] = dObj;

    QJsonDocument doc(iObj);
    m_webSocket->sendTextMessage(doc.toJson(QJsonDocument::Compact));
    qDebug() << "DiscordReader: Sent Identify payload.";
}

void DiscordReader::on_requestDiscordSend(const QString &channelId, const QString &text) {
    if (m_botToken.isEmpty()) return;

    QUrl url(QString("https://discord.com/api/v10/channels/%1/messages").arg(channelId));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bot %1").arg(m_botToken).toUtf8());
    request.setRawHeader("User-Agent", "DiscordBot (https://github.com/BLUE000/AiAssistantAvatar, 1.0)");

    QJsonObject payload;
    payload["content"] = text;

    QJsonDocument doc(payload);
    QByteArray payloadData = doc.toJson(QJsonDocument::Compact);

    qDebug() << "DiscordReader: Sending reply to channel" << channelId << ":" << text;
    m_networkManager->post(request, payloadData);
}

void DiscordReader::onReplyFinished(QNetworkReply *reply) {
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "DiscordReader: REST API error";
        qWarning() << "HTTP:" << status;
        qWarning() << "Qt Error:" << reply->error();
        qWarning() << "Error:" << reply->errorString();
        qWarning() << "Response:" << body;

        const auto headers = reply->rawHeaderPairs();
        for (const auto &h : headers) {
            qWarning() << h.first << ":" << h.second;
        }
    } else {
        qDebug() << "DiscordReader: Message sent successfully.";
        if (!body.isEmpty()) {
            qDebug() << "Response:" << body;
        }
    }
    reply->deleteLater();
}

void DiscordReader::sendGreeting() {
    if (m_channelId.isEmpty()) return;
    m_shouldGreet = false;
    m_lastGreetedChannelId = m_channelId;

    // 挨拶プロンプトを DiscordMessageReceived として発火 → AI が自然な挨拶文を生成してチャンネルへ送信する
    AppEvent event;
    event.type = EventType::DiscordMessageReceived;
    event.source = "DiscordReader";
    event.text   = "\uff08システム）Discordチャンネルに接続しました。メンバーに明るく挨拶してください。";

    QVariantMap meta;
    meta["channel_id"]  = m_channelId;
    meta["username"]    = "__system_greeting__";
    meta["user_id"]     = "";
    meta["is_greeting"] = true;
    event.extraData = meta;

    qDebug() << "DiscordReader: Emitting greeting event for channel" << m_channelId;
    emit notifyEvent(event);
}
