#include "discord_reader.h"
#include "utils/json_comment_remover.h"
#include "utils/config_utils.h"
#include "utils/process_utils.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCoreApplication>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QDebug>

DiscordReader::DiscordReader(QObject *parent)
    : QObject(parent), m_isRunning(false), m_enabled(false), m_lastSequence(0), m_hasAck(true)
{
    m_process = new QProcess(this);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &DiscordReader::onProcessOutputReady);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &DiscordReader::onProcessFinished);

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
    QString configPath = m_configPath;
    if (configPath.isEmpty()) {
        configPath = ConfigUtils::resolveConfigFilePath("local_settings.json");
    }

    if (!QFile::exists(configPath)) return;

    QFile file(configPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QByteArray data = JsonCommentRemover::stripHashComments(file.readAll());
        file.close();
        QJsonDocument doc = QJsonDocument::fromJson(data);

        if (!doc.isNull() && doc.isObject()) {
            QJsonObject obj = doc.object();
            m_enabled = obj.value("discord_enabled").toBool(false);
            m_botToken = obj.value("discord_bot_token").toString().trimmed();
            
            // 複数チャンネル設定の読み込み
            m_channels.clear();
            QJsonArray channelsArray = obj.value("discord_channels").toArray();
            for (int i = 0; i < channelsArray.size(); ++i) {
                QJsonObject chObj = channelsArray.at(i).toObject();
                ChannelConfig conf;
                conf.id = chObj.value("channel_id").toString().trimmed();
                conf.greetingEnabled = chObj.value("greeting_enabled").toBool(false);
                if (!conf.id.isEmpty()) {
                    m_channels.append(conf);
                }
            }

            // 後方互換フォールバック
            if (m_channels.isEmpty()) {
                ChannelConfig conf;
                conf.id = obj.value("discord_channel_id").toString().trimmed();
                bool fallbackGreet = obj.value("greeting_enabled").toBool(false);
                conf.greetingEnabled = obj.value("discord_greeting_enabled").toBool(fallbackGreet);
                m_channels.append(conf);
            }

            m_wakeWord = obj.value("twitch_wakeword").toString().trimmed();
            m_wakeWordMode = obj.value("twitch_wakeword_mode").toString("contains").trimmed();
            m_nameReactionEnabled = obj.value("name_reaction_enabled").toBool(true);
            m_avatarName = obj.value("avatar_name").toString().trimmed();
        }
    }
}

void DiscordReader::on_startReading() {
    loadSettings();
    if (!m_enabled || m_botToken.isEmpty() || m_channels.isEmpty()) {
        qDebug() << "DiscordReader: Disabled or configuration incomplete.";
        return;
    }

    if (m_isRunning) return;
    m_isRunning = true;

    QString exePath = ProcessUtils::resolveExecutablePath("DiscordObserver");
    if (!m_isMock && QFile::exists(exePath)) {
        m_useProcess = true;
        ProcessUtils::configureProcessEnvironment(*m_process);
        QStringList args;
        args << "--daemon";
        if (!m_configPath.isEmpty()) {
            args << "--config" << m_configPath;
        }
        qDebug() << "DiscordReader: Starting DiscordObserver subprocess:" << exePath << args;
        m_process->start(exePath, args);
        return;
    }

    m_useProcess = false;
    qDebug() << "DiscordReader: Starting internal WebSocket connection...";
    connectToDiscord();
}

void DiscordReader::on_stopReading() {
    m_isRunning = false;

    if (m_useProcess && m_process) {
        if (m_process->state() == QProcess::Running) {
            m_process->write("{\"action\":\"stop\"}\n");
            if (!m_process->waitForFinished(1000)) {
                m_process->terminate();
                if (!m_process->waitForFinished(1000)) {
                    m_process->kill();
                }
            }
        }
        m_useProcess = false;
    }

    m_heartbeatTimer->stop();

    disconnect(m_webSocket, &QWebSocket::disconnected, this, &DiscordReader::onWebSocketDisconnected);

    if (m_webSocket->state() != QAbstractSocket::UnconnectedState) {
        m_webSocket->close();
    }

    connect(m_webSocket, &QWebSocket::disconnected, this, &DiscordReader::onWebSocketDisconnected);

    qDebug() << "DiscordReader: Stopped.";
}

void DiscordReader::on_settingsUpdated() {
    qDebug() << "DiscordReader: Settings updated, reloading...";
    
    if (m_useProcess && m_process && m_process->state() == QProcess::Running) {
        loadSettings();
        m_process->write("{\"action\":\"reload\"}\n");
        return;
    }

    QSet<QString> prevChannelIds;
    for (const auto &ch : m_channels) {
        prevChannelIds.insert(ch.id);
    }

    bool wasRunning = m_isRunning;
    on_stopReading();
    loadSettings();

    bool channelListChanged = false;
    for (const auto &ch : m_channels) {
        if (!prevChannelIds.contains(ch.id)) {
            channelListChanged = true;
            m_greetedChannels.remove(ch.id); 
        }
    }

    if (channelListChanged) {
        qDebug() << "DiscordReader: Channels updated. Greeting scheduled for new channels.";
        m_shouldGreet = true;
    }

    if (wasRunning && m_enabled) {
        on_startReading();
    }
}

void DiscordReader::on_discordConnectRequested() {
    qDebug() << "DiscordReader: /discord connect requested. Greeting scheduled for all channels.";
    if (m_useProcess && m_process && m_process->state() == QProcess::Running) {
        m_process->write("{\"action\":\"connect\"}\n");
        return;
    }

    m_shouldGreet = true;
    m_greetedChannels.clear();
    on_stopReading();
    if (m_enabled) {
        on_startReading();
    }
}


void DiscordReader::connectToDiscord() {
    if (!m_isRunning) return;

    if (m_webSocket->state() != QAbstractSocket::UnconnectedState) {
        qDebug() << "DiscordReader: Already connecting or connected. State:" << m_webSocket->state();
        return;
    }

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
    
    if (obj.contains("s") && !obj.value("s").isNull()) {
        m_lastSequence = obj.value("s").toInt();
    }

    switch (op) {
        case 10: { // Hello
            QJsonObject dObj = obj.value("d").toObject();
            int intervalMs = dObj.value("heartbeat_interval").toInt(45000);
            qDebug() << "DiscordReader: Hello received. Heartbeat interval:" << intervalMs << "ms";
            
            sendHeartbeat();
            m_heartbeatTimer->start(intervalMs);

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
        case 0: { // Dispatch
            QString t = obj.value("t").toString();
            QJsonObject dObj = obj.value("d").toObject();

            if (t == "READY") {
                QJsonObject userObj = dObj.value("user").toObject();
                m_botUserId = userObj.value("id").toString();
                qDebug() << "DiscordReader: Bot is ready. Bot User ID:" << m_botUserId;
                if (m_shouldGreet) {
                    qDebug() << "DiscordReader: READY received. Triggering greetings.";
                    QTimer::singleShot(1000, this, [this]() { sendGreetings(); });
                }
            }
            else if (t == "MESSAGE_CREATE") {
                QString channelId = dObj.value("channel_id").toString();
                
                bool isTargetChannel = false;
                for (const auto &ch : m_channels) {
                    if (channelId == ch.id) {
                        isTargetChannel = true;
                        break;
                    }
                }
                
                if (isTargetChannel) {
                    QJsonObject authorObj = dObj.value("author").toObject();
                    QString authorId = authorObj.value("id").toString();
                    
                    if (authorId != m_botUserId && !authorObj.value("bot").toBool(false)) {
                        QString content = dObj.value("content").toString().trimmed();
                        QString username = authorObj.value("username").toString();

                        if (!content.isEmpty()) {
                            bool isMatch = false;
                            QString cleanMessage = content;

                            auto stripKeywordWithHonorifics = [](const QString &src, const QString &keyword, bool prefixOnly) -> QString {
                                if (keyword.isEmpty()) return src;
                                QRegularExpression regex((prefixOnly ? "^" : "") + QRegularExpression::escape(keyword) + "(?:くん|君|さん|ちゃん|様|たん|殿|氏|ー|〜)*[、。！？!?\\s\\t,.]*");
                                QString result = src;
                                result.replace(regex, "");
                                return result.trimmed();
                            };

                            if (!m_wakeWord.isEmpty()) {
                                if (m_wakeWordMode == "prefix" || m_wakeWordMode == "command") {
                                    if (content.startsWith(m_wakeWord)) {
                                        isMatch = true;
                                        cleanMessage = stripKeywordWithHonorifics(content, m_wakeWord, true);
                                    }
                                } else {
                                    if (content.contains(m_wakeWord)) {
                                        isMatch = true;
                                        cleanMessage = stripKeywordWithHonorifics(content, m_wakeWord, false);
                                    }
                                }
                            }

                            if (!isMatch && m_nameReactionEnabled && !m_avatarName.isEmpty()) {
                                if (content.contains(m_avatarName)) {
                                    isMatch = true;
                                    cleanMessage = stripKeywordWithHonorifics(content, m_avatarName, false);
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
    iObj["op"] = 2;

    QJsonObject dObj;
    dObj["token"] = m_botToken;
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
    if (m_useProcess && m_process && m_process->state() == QProcess::Running) {
        QJsonObject cmd;
        cmd["action"] = "send";
        cmd["channel_id"] = channelId;
        cmd["text"] = text;
        m_process->write(QJsonDocument(cmd).toJson(QJsonDocument::Compact) + "\n");
        return;
    }

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

void DiscordReader::sendGreetings() {
    m_shouldGreet = false;
    for (const auto &ch : m_channels) {
        if (ch.greetingEnabled && !m_greetedChannels.contains(ch.id)) {
            sendChannelGreeting(ch.id);
            m_greetedChannels.insert(ch.id);
        }
    }
}

void DiscordReader::sendChannelGreeting(const QString &channelId) {
    AppEvent event;
    event.type = EventType::DiscordMessageReceived;
    event.source = "DiscordReader";
    event.text   = "\uff08システム）Discordチャンネルに接続しました。メンバーに明るく挨拶してください。";

    QVariantMap meta;
    meta["channel_id"]  = channelId;
    meta["username"]    = "__system_greeting__";
    meta["user_id"]     = "";
    meta["is_greeting"] = true;
    event.extraData = meta;

    qDebug() << "DiscordReader: Emitting greeting event for channel" << channelId;
    emit notifyEvent(event);
}

void DiscordReader::onProcessOutputReady() {
    while (m_process && m_process->canReadLine()) {
        QByteArray line = m_process->readLine().trimmed();
        if (!line.isEmpty()) {
            handleSubprocessLine(QString::fromUtf8(line));
        }
    }
}

void DiscordReader::handleSubprocessLine(const QString &line) {
    QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
    if (doc.isNull() || !doc.isObject()) return;

    QJsonObject obj = doc.object();
    QString eventName = obj.value("event").toString();

    if (eventName == "message") {
        AppEvent event;
        event.type = EventType::DiscordMessageReceived;
        event.source = "DiscordReader";
        event.text = obj.value("text").toString();
        event.extraData["channel_id"] = obj.value("channel_id").toString();
        event.extraData["username"] = obj.value("username").toString();
        event.extraData["user_id"] = obj.value("user_id").toString();
        emit notifyEvent(event);
    } else if (eventName == "greeting") {
        QString channelId = obj.value("channel_id").toString();
        sendChannelGreeting(channelId);
    } else if (eventName == "ready") {
        qDebug() << "DiscordReader: DiscordObserver ready. Bot User ID:" << obj.value("bot_id").toString();
    } else if (eventName == "status") {
        qDebug() << "DiscordReader [Subprocess]:" << obj.value("message").toString();
    }
}

void DiscordReader::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    qDebug() << "DiscordReader: DiscordObserver subprocess finished. ExitCode:" << exitCode << "Status:" << exitStatus;
    if (m_isRunning) {
        QTimer::singleShot(3000, this, [this]() {
            if (m_isRunning) on_startReading();
        });
    }
}

