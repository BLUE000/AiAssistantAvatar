#include <QCoreApplication>
#include <QCommandLineParser>
#include <QWebSocket>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QSet>
#include <QRegularExpression>
#include <QThread>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <iostream>
#include <string>

#include "utils/config_utils.h"
#include "utils/json_comment_remover.h"

// 標準入力を非同期に読み取るワーカースレッド
class StdInWorker : public QThread {
    Q_OBJECT
public:
    explicit StdInWorker(QObject *parent = nullptr) : QThread(parent) {}

    void run() override {
#ifdef Q_OS_WIN
        HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
        char buffer[4096];
        std::string accumulated;
        while (!isInterruptionRequested()) {
            DWORD bytesRead = 0;
            BOOL success = ReadFile(hStdin, buffer, sizeof(buffer) - 1, &bytesRead, NULL);
            if (!success || bytesRead == 0) {
                // パイプ切断 (親プロセス終了またはEOF)
                emit eofReceived();
                break;
            }
            buffer[bytesRead] = '\0';
            accumulated.append(buffer, bytesRead);

            size_t pos = 0;
            while ((pos = accumulated.find('\n')) != std::string::npos) {
                std::string line = accumulated.substr(0, pos);
                accumulated.erase(0, pos + 1);

                QString qLine = QString::fromUtf8(line.c_str()).trimmed();
                if (!qLine.isEmpty()) {
                    emit lineReceived(qLine);
                }
            }
        }
#else
        std::string line;
        while (!isInterruptionRequested() && std::getline(std::cin, line)) {
            QString qLine = QString::fromStdString(line).trimmed();
            if (!qLine.isEmpty()) {
                emit lineReceived(qLine);
            }
        }
        emit eofReceived();
#endif
    }

signals:
    void lineReceived(const QString &line);
    void eofReceived();
};


// 常駐デーモンクラス
class DiscordGatewayDaemon : public QObject {
    Q_OBJECT
private:
    bool m_isRunning = false;
    bool m_enabled = false;
    QString m_botToken;
    QString m_configPath;

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
    QString m_botUserId;
    QString m_wakeWord;
    QString m_wakeWordMode;
    bool m_nameReactionEnabled = true;
    QString m_avatarName;
    bool m_shouldGreet = false;
    QSet<QString> m_greetedChannels;
    StdInWorker *m_stdInWorker = nullptr;

public:
    explicit DiscordGatewayDaemon(const QString &configPath, const QString &overrideToken, QObject *parent = nullptr)
        : QObject(parent), m_configPath(configPath), m_botToken(overrideToken)
    {
        m_webSocket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
        m_networkManager = new QNetworkAccessManager(this);
        m_heartbeatTimer = new QTimer(this);

        connect(m_webSocket, &QWebSocket::connected, this, &DiscordGatewayDaemon::onWebSocketConnected);
        connect(m_webSocket, &QWebSocket::disconnected, this, &DiscordGatewayDaemon::onWebSocketDisconnected);
        connect(m_webSocket, &QWebSocket::textMessageReceived, this, &DiscordGatewayDaemon::onTextMessageReceived);
        connect(m_networkManager, &QNetworkAccessManager::finished, this, &DiscordGatewayDaemon::onReplyFinished);
        connect(m_heartbeatTimer, &QTimer::timeout, this, &DiscordGatewayDaemon::sendHeartbeat);

        m_stdInWorker = new StdInWorker(this);
        connect(m_stdInWorker, &StdInWorker::lineReceived, this, &DiscordGatewayDaemon::onCommandReceived);
        connect(m_stdInWorker, &StdInWorker::eofReceived, this, [this]() {
            stop();
            std::exit(0);
        });

        loadSettings();
    }


    ~DiscordGatewayDaemon() override {
        stop();
    }

    void start() {
        if (m_isRunning) return;
        m_isRunning = true;

        m_stdInWorker->start();

        if (!m_enabled || m_botToken.isEmpty() || m_channels.isEmpty()) {
            sendStatus("warning", "DiscordObserver: Disabled or configuration incomplete. Waiting for configuration.");
            return;
        }

        connectToDiscord();
    }

    void stop() {
        if (!m_isRunning) return;
        m_isRunning = false;

        m_heartbeatTimer->stop();

        if (m_stdInWorker && m_stdInWorker->isRunning()) {
            m_stdInWorker->requestInterruption();
            m_stdInWorker->terminate();
            m_stdInWorker->wait(200);
        }

        disconnect(m_webSocket, &QWebSocket::disconnected, this, &DiscordGatewayDaemon::onWebSocketDisconnected);
        if (m_webSocket->state() != QAbstractSocket::UnconnectedState) {
            m_webSocket->close();
        }
    }


    void loadSettings() {
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
                if (m_botToken.isEmpty()) {
                    m_botToken = obj.value("discord_bot_token").toString().trimmed();
                }

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

                if (m_channels.isEmpty()) {
                    ChannelConfig conf;
                    conf.id = obj.value("discord_channel_id").toString().trimmed();
                    bool fallbackGreet = obj.value("greeting_enabled").toBool(false);
                    conf.greetingEnabled = obj.value("discord_greeting_enabled").toBool(fallbackGreet);
                    if (!conf.id.isEmpty()) {
                        m_channels.append(conf);
                    }
                }

                m_wakeWord = obj.value("twitch_wakeword").toString().trimmed();
                m_wakeWordMode = obj.value("twitch_wakeword_mode").toString("contains").trimmed();
                m_nameReactionEnabled = obj.value("name_reaction_enabled").toBool(true);
                m_avatarName = obj.value("avatar_name").toString().trimmed();
            }
        }
    }

public slots:
    void onCommandReceived(const QString &line) {
        QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
        if (doc.isNull() || !doc.isObject()) {
            sendStatus("warning", "DiscordObserver: Invalid JSON received on stdin: " + line);
            return;
        }

        QJsonObject obj = doc.object();
        QString action = obj.value("action").toString().trimmed().toLower();

        if (action == "send") {
            QString channelId = obj.value("channel_id").toString().trimmed();
            QString text = obj.value("text").toString();
            if (!channelId.isEmpty() && !text.isEmpty()) {
                sendMessage(channelId, text);
            }
        } else if (action == "reload") {
            loadSettings();
            sendStatus("info", "DiscordObserver: Settings reloaded.");
        } else if (action == "connect") {
            m_shouldGreet = true;
            m_greetedChannels.clear();
            if (m_webSocket->state() != QAbstractSocket::UnconnectedState) {
                m_webSocket->close();
            }
            connectToDiscord();
        } else if (action == "stop") {
            stop();
            std::exit(0);
        }
    }


private:
    void emitStdoutJson(const QJsonObject &obj) {
        QJsonDocument doc(obj);
        QByteArray utf8Data = doc.toJson(QJsonDocument::Compact);
        std::cout << utf8Data.constData() << "\n" << std::flush;
    }

    void sendStatus(const QString &level, const QString &message) {
        QJsonObject obj;
        obj["event"] = "status";
        obj["level"] = level;
        obj["message"] = message;
        emitStdoutJson(obj);
    }

    void connectToDiscord() {
        if (!m_isRunning) return;
        if (m_webSocket->state() != QAbstractSocket::UnconnectedState) return;

        QUrl url("wss://gateway.discord.gg/?v=10&encoding=json");
        m_webSocket->open(url);
    }

    void sendMessage(const QString &channelId, const QString &text) {
        if (m_botToken.isEmpty()) {
            sendStatus("error", "DiscordObserver: Cannot send message. Bot token missing.");
            return;
        }

        QUrl url(QString("https://discord.com/api/v10/channels/%1/messages").arg(channelId));
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Authorization", QString("Bot %1").arg(m_botToken).toUtf8());
        request.setRawHeader("User-Agent", "DiscordBot (https://github.com/BLUE000/AiAssistantAvatar, 1.0)");

        QJsonObject payload;
        payload["content"] = text;

        QByteArray payloadData = QJsonDocument(payload).toJson(QJsonDocument::Compact);
        m_networkManager->post(request, payloadData);
    }

    void sendHeartbeat() {
        if (m_webSocket->state() != QAbstractSocket::ConnectedState) return;

        if (!m_hasAck) {
            sendStatus("warning", "DiscordObserver: Heartbeat ACK missed. Reconnecting...");
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

        m_webSocket->sendTextMessage(QString::fromUtf8(QJsonDocument(hObj).toJson(QJsonDocument::Compact)));
    }

    void identify() {
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
        m_webSocket->sendTextMessage(QString::fromUtf8(QJsonDocument(iObj).toJson(QJsonDocument::Compact)));
    }

    void sendGreetings() {
        m_shouldGreet = false;
        for (const auto &ch : m_channels) {
            if (ch.greetingEnabled && !m_greetedChannels.contains(ch.id)) {
                QJsonObject gObj;
                gObj["event"] = "greeting";
                gObj["channel_id"] = ch.id;
                emitStdoutJson(gObj);
                m_greetedChannels.insert(ch.id);
            }
        }
    }

private slots:
    void onWebSocketConnected() {
        m_hasAck = true;
        sendStatus("info", "DiscordObserver: WebSocket connected to Gateway.");
    }

    void onWebSocketDisconnected() {
        m_heartbeatTimer->stop();
        sendStatus("warning", QString("DiscordObserver: WebSocket disconnected. CloseCode: %1").arg(m_webSocket->closeCode()));

        if (m_isRunning && m_webSocket->state() == QAbstractSocket::UnconnectedState) {
            QTimer::singleShot(5000, this, [this]() {
                if (m_isRunning) connectToDiscord();
            });
        }
    }

    void onTextMessageReceived(const QString &message) {
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
                m_webSocket->close();
                break;
            }
            case 0: { // Dispatch
                QString t = obj.value("t").toString();
                QJsonObject dObj = obj.value("d").toObject();

                if (t == "READY") {
                    QJsonObject userObj = dObj.value("user").toObject();
                    m_botUserId = userObj.value("id").toString();
                    QString botUsername = userObj.value("username").toString();

                    QJsonObject readyObj;
                    readyObj["event"] = "ready";
                    readyObj["bot_id"] = m_botUserId;
                    readyObj["username"] = botUsername;
                    emitStdoutJson(readyObj);

                    if (m_shouldGreet) {
                        QTimer::singleShot(1000, this, [this]() { sendGreetings(); });
                    }
                } else if (t == "MESSAGE_CREATE") {
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
                                    QJsonObject msgObj;
                                    msgObj["event"] = "message";
                                    msgObj["channel_id"] = channelId;
                                    msgObj["username"] = username;
                                    msgObj["user_id"] = authorId;
                                    msgObj["text"] = cleanMessage;
                                    msgObj["raw_content"] = content;
                                    emitStdoutJson(msgObj);
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

    void onReplyFinished(QNetworkReply *reply) {
        if (reply->error() != QNetworkReply::NoError) {
            sendStatus("error", QString("DiscordObserver: REST API error: %1 (%2)").arg(reply->errorString()).arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()));
        }
        reply->deleteLater();
    }
};

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("DiscordObserver");
    app.setApplicationVersion("1.0.0");

    // 親ディレクトリをライブラリ探索パスに追加 (tools/ 配置対応)
    app.addLibraryPath(app.applicationDirPath() + "/..");

    QCommandLineParser parser;
    parser.setApplicationDescription("DiscordObserver - Standalone Discord Gateway & REST CLI tool");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption daemonOption(QStringList() << "d" << "daemon", "Run in daemon mode with stdin/stdout IPC (default)");
    parser.addOption(daemonOption);

    QCommandLineOption sendOption(QStringList() << "s" << "send", "Run in one-shot message send mode");
    parser.addOption(sendOption);

    QCommandLineOption channelOption("channel", "Destination Discord channel ID for --send mode", "channel_id");
    parser.addOption(channelOption);

    QCommandLineOption textOption(QStringList() << "t" << "text", "Message text to send for --send mode", "text");
    parser.addOption(textOption);

    QCommandLineOption configOption(QStringList() << "c" << "config", "Path to local_settings.json", "path");
    parser.addOption(configOption);

    QCommandLineOption tokenOption(QStringList() << "k" << "bot-token", "Discord Bot token", "token");
    parser.addOption(tokenOption);

    parser.process(app);

    bool isSendMode = parser.isSet(sendOption);
    QString configPath = parser.value(configOption);
    QString botToken = parser.value(tokenOption);

    // 設定ファイルからの Bot トークン補完
    if (botToken.isEmpty()) {
        QString resolvedConfig = configPath.isEmpty() ? ConfigUtils::resolveConfigFilePath("local_settings.json") : configPath;
        if (QFile::exists(resolvedConfig)) {
            QFile file(resolvedConfig);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QByteArray data = JsonCommentRemover::stripHashComments(file.readAll());
                file.close();
                QJsonDocument doc = QJsonDocument::fromJson(data);
                if (!doc.isNull() && doc.isObject()) {
                    botToken = doc.object().value("discord_bot_token").toString().trimmed();
                }
            }
        }
    }

    if (isSendMode) {
        QString channelId = parser.value(channelOption).trimmed();
        QString text = parser.value(textOption).trimmed();

        if (channelId.isEmpty() || text.isEmpty()) {
            std::cerr << "Error: --channel and --text are required for --send mode\n";
            return 2;
        }

        if (botToken.isEmpty()) {
            std::cerr << "Error: Discord bot token is missing or not configured\n";
            return 1;
        }

        QNetworkAccessManager nam;
        QUrl url(QString("https://discord.com/api/v10/channels/%1/messages").arg(channelId));
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Authorization", QString("Bot %1").arg(botToken).toUtf8());
        request.setRawHeader("User-Agent", "DiscordBot (https://github.com/BLUE000/AiAssistantAvatar, 1.0)");

        QJsonObject payload;
        payload["content"] = text;
        QByteArray payloadData = QJsonDocument(payload).toJson(QJsonDocument::Compact);

        QNetworkReply *reply = nam.post(request, payloadData);
        int exitCode = 0;

        QObject::connect(reply, &QNetworkReply::finished, [&]() {
            if (reply->error() != QNetworkReply::NoError) {
                std::cerr << "Error: " << reply->errorString().toUtf8().constData()
                          << " (HTTP " << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() << ")\n";
                exitCode = 1;
            } else {
                std::cout << "{\"status\":\"success\",\"channel_id\":\"" << channelId.toUtf8().constData() << "\"}\n";
                exitCode = 0;
            }
            reply->deleteLater();
            app.quit();
        });

        app.exec();
        return exitCode;
    }

    // デフォルト: 常駐デーモンモード
    DiscordGatewayDaemon daemon(configPath, botToken);
    daemon.start();

    return app.exec();
}

#include "discord_observer_main.moc"
