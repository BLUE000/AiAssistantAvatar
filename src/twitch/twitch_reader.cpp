#include "twitch_reader.h"
#include <QDebug>
#include <QTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDesktopServices>
#include <QUrl>
#include <QTcpSocket>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QCoreApplication>

TwitchReader::TwitchReader(QObject *parent) 
    : QObject(parent), m_wakeWord("アバターさん") 
{
}

TwitchReader::~TwitchReader() {
    on_stopReading();
}

void TwitchReader::setSettings(const QString &channel, const QString &token, const QString &clientId, const QString &wakeWord) {
    m_channel = channel;
    m_oauthToken = token;
    m_clientId = clientId;
    m_wakeWord = wakeWord;
    qDebug() << "TwitchReader: Settings updated manually. Channel:" << channel << "WakeWord:" << wakeWord;
}

void TwitchReader::loadSettings() {
    m_configPath = "local_settings.json";
#ifdef PROJECT_SOURCE_DIR
    if (!QFile::exists(m_configPath)) {
        m_configPath = QString(PROJECT_SOURCE_DIR) + "/local_settings.json";
    }
#endif
    if (!QFile::exists(m_configPath)) {
        m_configPath = QCoreApplication::applicationDirPath() + "/local_settings.json";
    }
    if (!QFile::exists(m_configPath)) {
        m_configPath = QCoreApplication::applicationDirPath() + "/../local_settings.json";
    }
    if (!QFile::exists(m_configPath)) {
        m_configPath = QCoreApplication::applicationDirPath() + "/../../local_settings.json";
    }

    QFile file(m_configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "TwitchReader: local_settings.json not found or unable to open. Tried path:" << m_configPath;
        return;
    }
    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isNull() && doc.isObject()) {
        QJsonObject obj = doc.object();
        m_clientId = obj.value("twitch_client_id").toString().trimmed();
        m_oauthToken = obj.value("twitch_oauth_token").toString().trimmed();
        m_channel = obj.value("twitch_channel").toString().trimmed();
        m_authPort = obj.value("twitch_port").toInt(48080);
        m_wakeWord = obj.value("twitch_wakeword").toString("アバターさん").trimmed();
        if (m_wakeWord.isEmpty()) {
            m_wakeWord = "アバターさん";
        }
        m_wakeWordMode = obj.value("twitch_wakeword_mode").toString("contains").trimmed().toLower();
        if (m_wakeWordMode.isEmpty()) {
            m_wakeWordMode = "contains";
        }
    }
}

void TwitchReader::saveTokenToSettings(const QString &token) {
    m_oauthToken = token;
    
    if (m_configPath.isEmpty()) {
        m_configPath = "local_settings.json";
#ifdef PROJECT_SOURCE_DIR
        if (!QFile::exists(m_configPath)) {
            m_configPath = QString(PROJECT_SOURCE_DIR) + "/local_settings.json";
        }
#endif
        if (!QFile::exists(m_configPath)) {
            m_configPath = QCoreApplication::applicationDirPath() + "/local_settings.json";
        }
        if (!QFile::exists(m_configPath)) {
            m_configPath = QCoreApplication::applicationDirPath() + "/../local_settings.json";
        }
        if (!QFile::exists(m_configPath)) {
            m_configPath = QCoreApplication::applicationDirPath() + "/../../local_settings.json";
        }
    }

    QFile file(m_configPath);
    QByteArray data;
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        data = file.readAll();
        file.close();
    }

    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject obj;
    if (!doc.isNull() && doc.isObject()) {
        obj = doc.object();
    }
    
    obj["twitch_oauth_token"] = token;
    
    QJsonDocument newDoc(obj);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(newDoc.toJson(QJsonDocument::Indented));
        file.close();
        qDebug() << "TwitchReader: OAuth token saved to" << m_configPath;
    } else {
        qWarning() << "TwitchReader: Failed to save OAuth token to" << m_configPath;
    }
}

void TwitchReader::startOAuthServer() {
    if (m_authServer) {
        m_authServer->close();
        delete m_authServer;
        m_authServer = nullptr;
    }

    m_authServer = new QTcpServer(this);
    connect(m_authServer, &QTcpServer::newConnection, this, &TwitchReader::handleNewConnection);

    if (!m_authServer->listen(QHostAddress::LocalHost, m_authPort)) {
        qCritical() << "TwitchReader: Failed to start OAuth local server on port" << m_authPort;
        AppEvent event;
        event.type = EventType::ErrorOccurred;
        event.source = "TwitchReader";
        event.text = QString("OAuth ローカルサーバーの起動に失敗しました（ポート %1 が使用中）。").arg(m_authPort);
        emit notifyEvent(event);
        return;
    }
    qDebug() << "TwitchReader: OAuth local server listening on port" << m_authPort;

    // Twitch認証URLをブラウザで開く
    QString authUrl = QString("https://id.twitch.tv/oauth2/authorize"
                              "?client_id=%1"
                              "&redirect_uri=http://localhost:%2/"
                              "&response_type=token"
                              "&scope=chat:read")
                      .arg(m_clientId)
                      .arg(m_authPort);

    qDebug() << "TwitchReader: Opening browser for OAuth authentication...";
    QDesktopServices::openUrl(QUrl(authUrl));
}

void TwitchReader::handleNewConnection() {
    if (!m_authServer) return;
    QTcpSocket *socket = m_authServer->nextPendingConnection();
    if (!socket) return;

    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
        QByteArray requestData = socket->readAll();
        QString requestStr = QString::fromUtf8(requestData);

        QStringList lines = requestStr.split("\r\n");
        if (lines.isEmpty()) return;

        QString firstLine = lines.first();
        QStringList requestParts = firstLine.split(" ");
        if (requestParts.size() < 2) return;

        QString path = requestParts.at(1);
        qDebug() << "TwitchReader: Received HTTP request path:" << path;

        if (path.startsWith("/token")) {
            QUrl url("http://localhost" + path);
            QUrlQuery query(url.query());
            QString token = query.queryItemValue("access_token");

            if (!token.isEmpty()) {
                qDebug() << "TwitchReader: Successfully received access token.";
                saveTokenToSettings(token);

                QByteArray html = "HTTP/1.1 200 OK\r\n"
                                  "Content-Type: text/html; charset=utf-8\r\n"
                                  "Connection: close\r\n\r\n"
                                  "<html><head><title>Authentication Successful</title></head>"
                                  "<body><h2 style='color: green; font-family: sans-serif; text-align: center; margin-top: 50px;'>認証に成功しました！</h2>"
                                  "<p style='text-align: center; font-family: sans-serif; color: #555;'>このブラウザタブを閉じて、アプリケーションに戻ってください。</p></body></html>";
                socket->write(html);
                socket->disconnectFromHost();

                // サーバー停止とチャット接続開始
                QTimer::singleShot(1000, this, [this]() {
                    if (m_authServer) {
                        m_authServer->close();
                        m_authServer->deleteLater();
                        m_authServer = nullptr;
                    }
                    connectToTwitch();
                });
            } else {
                QByteArray html = "HTTP/1.1 400 Bad Request\r\n"
                                  "Content-Type: text/html; charset=utf-8\r\n"
                                  "Connection: close\r\n\r\n"
                                  "<html><body><h2>Error: Missing access token.</h2></body></html>";
                socket->write(html);
                socket->disconnectFromHost();
            }
        } else {
            // Implicit flow のハッシュフラグメントを処理するための HTML (JavaScript) を返却
            QByteArray html = "HTTP/1.1 200 OK\r\n"
                              "Content-Type: text/html; charset=utf-8\r\n"
                              "Connection: close\r\n\r\n"
                              "<html><head>"
                              "<script>"
                              "  const hash = window.location.hash;"
                              "  if (hash) {"
                              "    const params = new URLSearchParams(hash.substring(1));"
                              "    const token = params.get('access_token');"
                              "    if (token) {"
                              "      window.location.href = '/token?access_token=' + token;"
                              "    } else {"
                              "      document.write('アクセストークンの取得に失敗しました。');"
                              "    }"
                              "  } else {"
                              "    document.write('認証待機中...');"
                              "  }"
                              "</script>"
                              "</head><body></body></html>";
            socket->write(html);
            socket->disconnectFromHost();
        }
    });
}

void TwitchReader::connectToTwitch() {
    if (m_webSocket) {
        m_webSocket->close();
        delete m_webSocket;
        m_webSocket = nullptr;
    }

    m_webSocket = new QWebSocket();
    connect(m_webSocket, &QWebSocket::connected, this, &TwitchReader::onWebSocketConnected);
    connect(m_webSocket, &QWebSocket::disconnected, this, &TwitchReader::onWebSocketDisconnected);
    connect(m_webSocket, &QWebSocket::textMessageReceived, this, &TwitchReader::onTextMessageReceived);

    connect(m_webSocket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
        qWarning() << "TwitchReader: WebSocket error occurred:" << error << m_webSocket->errorString();
        AppEvent event;
        event.type = EventType::ErrorOccurred;
        event.source = "TwitchReader";
        event.text = "Twitch チャットサーバー接続でエラーが発生しました: " + m_webSocket->errorString();
        emit notifyEvent(event);
    });

    QUrl url("wss://irc-ws.chat.twitch.tv:443");
    qDebug() << "TwitchReader: Connecting to Twitch WebSocket at" << url.toString();
    m_webSocket->open(url);
}

void TwitchReader::onWebSocketConnected() {
    qDebug() << "TwitchReader: WebSocket connected. Authenticating...";

    m_webSocket->sendTextMessage("PASS oauth:" + m_oauthToken);
    m_webSocket->sendTextMessage("NICK justinfan12345"); // 読み取り専用としてのニックネーム
    
    QString channelLower = m_channel.toLower();
    if (!channelLower.startsWith("#")) {
        channelLower = "#" + channelLower;
    }
    m_webSocket->sendTextMessage("JOIN " + channelLower);
    
    qDebug() << "TwitchReader: Sent JOIN for channel" << channelLower;
}

void TwitchReader::onWebSocketDisconnected() {
    qDebug() << "TwitchReader: WebSocket disconnected.";
    if (m_isRunning) {
        qDebug() << "TwitchReader: Reconnecting in 5 seconds...";
        QTimer::singleShot(5000, this, [this]() {
            if (m_isRunning) connectToTwitch();
        });
    }
}

void TwitchReader::onTextMessageReceived(const QString &message) {
    QStringList lines = message.split("\r\n");
    for (const QString &line : lines) {
        if (line.isEmpty()) continue;

        if (line.startsWith("PING")) {
            m_webSocket->sendTextMessage("PONG :tmi.twitch.tv");
            continue;
        }

        // IRC メッセージパーサー
        QRegularExpression regex("^:([^!]+)![^ ]+ PRIVMSG #[^ ]+ :(.+)$");
        QRegularExpressionMatch match = regex.match(line);
        if (match.hasMatch()) {
            QString user = match.captured(1);
            QString comment = match.captured(2);
            injectTestComment(user, comment);
        }
    }
}

void TwitchReader::on_startReading() {
    if (m_isRunning) return;
    m_isRunning = true;
    
    loadSettings();

    if (m_oauthToken.isEmpty() || m_oauthToken == "YOUR_TWITCH_OAUTH_TOKEN") {
        if (m_clientId.isEmpty() || m_clientId == "YOUR_TWITCH_CLIENT_ID") {
            qCritical() << "TwitchReader: Client ID is missing. Cannot start OAuth process.";
            AppEvent event;
            event.type = EventType::ErrorOccurred;
            event.source = "TwitchReader";
            event.text = "Twitch クライアントIDが設定されていないため、認証を開始できません。";
            emit notifyEvent(event);
            return;
        }
        startOAuthServer();
    } else {
        connectToTwitch();
    }
}

void TwitchReader::on_stopReading() {
    if (!m_isRunning) return;
    m_isRunning = false;
    qDebug() << "TwitchReader: Stopping module...";

    if (m_authServer) {
        m_authServer->close();
        m_authServer->deleteLater();
        m_authServer = nullptr;
    }
    if (m_webSocket) {
        m_webSocket->close();
        m_webSocket->deleteLater();
        m_webSocket = nullptr;
    }
}

void TwitchReader::injectTestComment(const QString &user, const QString &message) {
    qDebug() << "TwitchReader: Injected comment from" << user << ":" << message;

    if (m_wakeWord.isEmpty()) return;

    bool isMatch = false;
    QString cleanMessage = message;

    if (m_wakeWordMode == "prefix" || m_wakeWordMode == "command") {
        if (message.startsWith(m_wakeWord)) {
            isMatch = true;
            cleanMessage = message.mid(m_wakeWord.length()).trimmed();
        }
    } else {
        if (message.contains(m_wakeWord)) {
            isMatch = true;
            cleanMessage = message;
            cleanMessage.replace(m_wakeWord, "");
            cleanMessage = cleanMessage.trimmed();
        }
    }

    if (isMatch) {
        AppEvent event;
        event.type = EventType::TwitchCommentReceived;
        event.source = "TwitchReader";
        event.text = cleanMessage;
        
        QVariantMap meta;
        meta["user"] = user;
        meta["raw_message"] = message;
        event.extraData = meta;

        emit notifyEvent(event);
    }
}
