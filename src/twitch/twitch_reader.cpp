#include "twitch_reader.h"
#include <QDebug>
#include <QTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDesktopServices>
#include <QUrl>
#include <QTcpSocket>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>


TwitchReader::TwitchReader(QObject *parent) 
    : QObject(parent), m_wakeWord("アバターさん") 
{
}

TwitchReader::~TwitchReader() {
    on_stopReading();
}

void TwitchReader::setSettings(const QString &channel, const QString &token, const QString &clientSecret, const QString &clientId, const QString &wakeWord) {
    m_channel = channel;
    m_oauthToken = token;
    m_clientSecret = clientSecret;
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
        m_clientSecret = obj.value("twitch_client_secret").toString().trimmed();
        m_oauthToken = obj.value("twitch_oauth_token").toString().trimmed();
        m_refreshToken = obj.value("twitch_refresh_token").toString().trimmed();
        m_channel = obj.value("twitch_channel").toString().trimmed();
        m_authPort = obj.value("twitch_port").toInt(48080);
        if (obj.contains("twitch_wakeword")) {
            m_wakeWord = obj.value("twitch_wakeword").toString().trimmed();
        } else {
            m_wakeWord = "アバターさん";
        }
        m_wakeWordMode = obj.value("twitch_wakeword_mode").toString("contains").trimmed().toLower();
        if (m_wakeWordMode.isEmpty()) {
            m_wakeWordMode = "contains";
        }
    }
}

void TwitchReader::saveTokenToSettings(const QString &accessToken, const QString &refreshToken) {
    Q_UNUSED(accessToken);
    Q_UNUSED(refreshToken);
    // スレッド競合を防ぐため、ファイル直接書き込みは廃止。UIスレッド側のイベント受信によって安全に保存されます。
}

void TwitchReader::startOAuthServer() {
    if (m_authServer) {
        m_authServer->close();
        delete m_authServer;
        m_authServer = nullptr;
    }

    m_authServer = new QTcpServer(this);
    connect(m_authServer, &QTcpServer::newConnection, this, &TwitchReader::handleNewConnection);

    if (!m_authServer->listen(QHostAddress::Any, m_authPort)) {
        qCritical() << "TwitchReader: Failed to start OAuth local server on port" << m_authPort;
        AppEvent event;
        event.type = EventType::ErrorOccurred;
        event.source = "TwitchReader";
        event.text = QString("OAuth ローカルサーバーの起動に失敗しました（ポート %1 が使用中）。").arg(m_authPort);
        emit notifyEvent(event);
        return;
    }
    qDebug() << "TwitchReader: OAuth local server listening on port" << m_authPort;

    // Twitch認証URLをブラウザで開く（Authorization Code Flow & force_verify=true）
    QString authUrl = QString("https://id.twitch.tv/oauth2/authorize"
                              "?client_id=%1"
                              "&redirect_uri=http://localhost:%2/"
                              "&response_type=code"
                              "&scope=chat:read+chat:edit"
                              "&force_verify=true")
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

        QUrl url("http://localhost" + path);
        QUrlQuery query(url.query());

        if (query.hasQueryItem("code")) {
            QString code = query.queryItemValue("code");
            qDebug() << "TwitchReader: Successfully received Authorization Code:" << code;

            QByteArray html = "HTTP/1.1 200 OK\r\n"
                              "Content-Type: text/html; charset=utf-8\r\n"
                              "Connection: close\r\n\r\n"
                              "<html><head><title>Authentication Successful</title></head>"
                              "<body><h2 style='color: green; font-family: sans-serif; text-align: center; margin-top: 50px;'>認証コードを受信しました！</h2>"
                              "<p style='text-align: center; font-family: sans-serif; color: #555;'>アプリでトークン取得を開始します。このブラウザタブを閉じて、アプリケーションに戻ってください。</p></body></html>";
            socket->write(html);
            socket->disconnectFromHost();

            // コードを用いてトークンを取得
            QTimer::singleShot(10, this, [this, code]() {
                requestTokensWithCode(code);
            });

            // サーバー停止をタイマーで実行
            QTimer::singleShot(1000, this, [this]() {
                if (m_authServer) {
                    m_authServer->close();
                    m_authServer->deleteLater();
                    m_authServer = nullptr;
                }
            });
        } else {
            QByteArray html = "HTTP/1.1 200 OK\r\n"
                              "Content-Type: text/html; charset=utf-8\r\n"
                              "Connection: close\r\n\r\n"
                              "<html><body><h2>Error: Missing authorization code.</h2>"
                              "<p>認証コードが取得できませんでした。認可し直してください。</p></body></html>";
            socket->write(html);
            socket->disconnectFromHost();
        }
    });
}

void TwitchReader::requestTokensWithCode(const QString &code) {
    if (!m_networkManager) {
        m_networkManager = new QNetworkAccessManager(this);
    }
    
    QUrl url("https://id.twitch.tv/oauth2/token");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QUrlQuery postData;
    postData.addQueryItem("client_id", m_clientId);
    postData.addQueryItem("client_secret", m_clientSecret);
    postData.addQueryItem("code", code);
    postData.addQueryItem("grant_type", "authorization_code");
    postData.addQueryItem("redirect_uri", QString("http://localhost:%1/").arg(m_authPort));

    QNetworkReply *reply = m_networkManager->post(request, postData.toString(QUrl::FullyEncoded).toUtf8());
    reply->setProperty("type", "authorization_code");
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onTokenRequestFinished(reply);
    });
}

void TwitchReader::refreshTwitchToken() {
    if (m_refreshToken.isEmpty()) {
        qWarning() << "TwitchReader: Cannot refresh token because refresh token is empty.";
        AppEvent event;
        event.type = EventType::ErrorOccurred;
        event.source = "TwitchReader";
        event.text = "Twitch リフレッシュトークンがありません。再認証を行ってください。";
        emit notifyEvent(event);
        return;
    }

    if (!m_networkManager) {
        m_networkManager = new QNetworkAccessManager(this);
    }

    qDebug() << "TwitchReader: Refreshing Twitch Access Token...";

    QUrl url("https://id.twitch.tv/oauth2/token");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QUrlQuery postData;
    postData.addQueryItem("grant_type", "refresh_token");
    postData.addQueryItem("refresh_token", m_refreshToken);
    postData.addQueryItem("client_id", m_clientId);
    postData.addQueryItem("client_secret", m_clientSecret);

    QNetworkReply *reply = m_networkManager->post(request, postData.toString(QUrl::FullyEncoded).toUtf8());
    reply->setProperty("type", "refresh_token");
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onTokenRequestFinished(reply);
    });
}

void TwitchReader::onTokenRequestFinished(QNetworkReply *reply) {
    reply->deleteLater();
    QString reqType = reply->property("type").toString();

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "TwitchReader: Token request/refresh failed:" << reply->errorString();
        QByteArray errData = reply->readAll();
        qWarning() << "Response:" << errData;

        AppEvent event;
        event.type = EventType::ErrorOccurred;
        event.source = "TwitchReader";
        if (reqType == "refresh_token") {
            event.text = "Twitch トークンの自動更新に失敗しました。再認証を行ってください。";
        } else {
            event.text = "Twitch トークンの取得に失敗しました: " + reply->errorString();
        }
        emit notifyEvent(event);
        return;
    }

    QByteArray responseData = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    if (doc.isNull() || !doc.isObject()) {
        qWarning() << "TwitchReader: Token response is invalid JSON.";
        return;
    }

    QJsonObject obj = doc.object();
    QString accessToken = obj.value("access_token").toString();
    QString refreshToken = obj.value("refresh_token").toString();

    if (accessToken.isEmpty()) {
        qWarning() << "TwitchReader: Token response missing access_token.";
        return;
    }

    m_oauthToken = accessToken;
    if (!refreshToken.isEmpty()) {
        m_refreshToken = refreshToken;
    }

    qDebug() << "TwitchReader: Token obtained/refreshed successfully. Type:" << reqType;

    if (reqType == "authorization_code") {
        fetchChannelName(accessToken);
    } else {
        // リフレッシュ時の接続
        connectToTwitch();

        // UIにトークンが自動更新されたことを通知して保存させる
        AppEvent event;
        event.type = EventType::SettingsUpdated;
        event.source = "TwitchReader";
        event.text = "Twitch OAuthトークンが自動更新されました。";
        
        QVariantMap meta;
        meta["twitch_oauth_token"] = m_oauthToken;
        meta["twitch_refresh_token"] = m_refreshToken;
        event.extraData = meta;
        emit notifyEvent(event);
    }
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

void TwitchReader::saveOAuthDataToSettings(const QString &accessToken, const QString &refreshToken, const QString &channel) {
    Q_UNUSED(accessToken);
    Q_UNUSED(refreshToken);
    Q_UNUSED(channel);
    // スレッド競合を防ぐため、ファイル直接書き込みは廃止。UIスレッド側のイベント受信によって安全に保存されます。
}

void TwitchReader::fetchChannelName(const QString &token) {
    QNetworkAccessManager *manager = new QNetworkAccessManager(this);
    QNetworkRequest request(QUrl("https://api.twitch.tv/helix/users"));
    request.setRawHeader("Authorization", ("Bearer " + token).toUtf8());
    request.setRawHeader("Client-Id", m_clientId.toUtf8());

    connect(manager, &QNetworkAccessManager::finished, this, [this, token, manager](QNetworkReply *reply) {
        QString channelName;
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray response = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(response);
            if (!doc.isNull() && doc.isObject()) {
                QJsonArray dataArray = doc.object().value("data").toArray();
                if (!dataArray.isEmpty()) {
                    channelName = dataArray.at(0).toObject().value("login").toString();
                    qDebug() << "TwitchReader: Retrieved channel name from Twitch Helix API:" << channelName;
                }
            }
            if (channelName.isEmpty()) {
                qWarning() << "TwitchReader: Helix API response was valid JSON but missing user data.";
            }
        } else {
            qWarning() << "TwitchReader: Failed to fetch channel name from Helix API:" << reply->errorString();
        }

        m_oauthToken = token;
        if (!channelName.isEmpty()) {
            m_channel = channelName;
        }

        // トークンとチャンネルが揃ったので接続開始
        connectToTwitch();

        // UIに設定が更新されたことを通知する
        AppEvent event;
        event.type = EventType::SettingsUpdated;
        event.source = "TwitchReader";
        event.text = "Twitch OAuth設定が更新されました。";
        
        QVariantMap meta;
        meta["twitch_oauth_token"] = token;
        meta["twitch_refresh_token"] = m_refreshToken;
        if (!channelName.isEmpty()) {
            meta["twitch_channel"] = channelName;
        }
        event.extraData = meta;
        
        emit notifyEvent(event);

        reply->deleteLater();
        manager->deleteLater();
    });

    manager->get(request);
}

void TwitchReader::on_settingsUpdated() {
    qDebug() << "TwitchReader: Settings updated. Reloading config.";
    loadSettings();
    if (m_isRunning) {
        connectToTwitch();
    }
}

void TwitchReader::on_twitchReauthRequested() {
    qDebug() << "TwitchReader: Re-authorization requested.";
    on_stopReading();
    
    // トークンをクリア
    m_oauthToken = "";
    m_refreshToken = "";
    
    // UIにトークンがクリアされたことを通知して保存させる
    AppEvent event;
    event.type = EventType::SettingsUpdated;
    event.source = "TwitchReader";
    event.text = "Twitchトークンがクリアされました。";
    QVariantMap meta;
    meta["twitch_oauth_token"] = "";
    meta["twitch_refresh_token"] = "";
    event.extraData = meta;
    emit notifyEvent(event);
    
    m_isRunning = true;
    
    if (m_clientId.isEmpty() || m_clientId == "YOUR_TWITCH_CLIENT_ID") {
        qCritical() << "TwitchReader: Client ID is missing. Cannot start OAuth process.";
        AppEvent errEvent;
        errEvent.type = EventType::ErrorOccurred;
        errEvent.source = "TwitchReader";
        errEvent.text = "Twitch クライアントIDが設定されていないため、認証を開始できません。";
        emit notifyEvent(errEvent);
        return;
    }
    startOAuthServer();
}

