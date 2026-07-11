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
    : QObject(parent), m_wakeWord(ConfigDefaults::WAKE_WORD) 
{
}

TwitchReader::~TwitchReader() {
    on_stopReading();
}

void TwitchReader::setSettings(const QString &channel, const QString &token, const QString &clientId, const QString &wakeWord, const QString &avatarName, bool nameReactionEnabled) {
    m_channel = channel;
    m_oauthToken = token;
    m_clientId = clientId;
    m_wakeWord = wakeWord;
    m_avatarName = avatarName;
    m_nameReactionEnabled = nameReactionEnabled;
    qDebug() << "TwitchReader: Settings updated manually. Channel:" << channel << "WakeWord:" << wakeWord << "AvatarName:" << avatarName << "NameReactionEnabled:" << nameReactionEnabled;
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
        m_botName = obj.value("twitch_username").toString().trimmed();
        m_authPort = obj.value("twitch_port").toInt(ConfigDefaults::TWITCH_PORT);
        if (obj.contains("twitch_wakeword")) {
            m_wakeWord = obj.value("twitch_wakeword").toString().trimmed();
        } else {
            m_wakeWord = ConfigDefaults::WAKE_WORD;
        }
        m_wakeWordMode = obj.value("twitch_wakeword_mode").toString(ConfigDefaults::WAKE_WORD_MODE).trimmed().toLower();
        if (m_wakeWordMode.isEmpty()) {
            m_wakeWordMode = ConfigDefaults::WAKE_WORD_MODE;
        }
        m_avatarName = obj.value("avatar_name").toString("AIアシスタント").trimmed();
        m_nameReactionEnabled = obj.value("name_reaction_enabled").toBool(true);
    }
}

void TwitchReader::saveTokenToSettings(const QString &accessToken) {
    Q_UNUSED(accessToken);
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

    // Twitch認証URLをブラウザで開く（Implicit Flow & force_verify=true）
    QString authUrl = QString("https://id.twitch.tv/oauth2/authorize"
                              "?client_id=%1"
                              "&redirect_uri=http://localhost:%2/"
                              "&response_type=token"
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

        if (path.startsWith("/token")) {
            // JavaScriptでフラグメントから抽出されたアクセストークンを受け取るエンドポイント
            if (query.hasQueryItem("access_token")) {
                QString token = query.queryItemValue("access_token");
                qDebug() << "TwitchReader: Successfully received Access Token via redirect:" << token;

                QByteArray html = "HTTP/1.1 200 OK\r\n"
                                  "Content-Type: text/html; charset=utf-8\r\n"
                                  "Connection: close\r\n\r\n"
                                  "<html><head><title>Authentication Successful</title></head>"
                                  "<body><h2 style='color: green; font-family: sans-serif; text-align: center; margin-top: 50px;'>認証が完了しました！</h2>"
                                  "<p style='text-align: center; font-family: sans-serif; color: #555;'>アプリケーションにトークンを転送しました。このブラウザタブを閉じて、アプリケーションに戻ってください。</p></body></html>";
                socket->write(html);
                socket->disconnectFromHost();

                // トークン取得後の処理
                QTimer::singleShot(10, this, [this, token]() {
                    fetchChannelName(token);
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
                                  "<html><body><h2>Error: Missing access token.</h2>"
                                  "<p>アクセストークンが取得できませんでした。</p></body></html>";
                socket->write(html);
                socket->disconnectFromHost();
            }
        } else {
            // 初回アクセス（ハッシュパース用JavaScript付きHTMLをブラウザへ返す）
            QByteArray html = "HTTP/1.1 200 OK\r\n"
                              "Content-Type: text/html; charset=utf-8\r\n"
                              "Connection: close\r\n\r\n"
                              "<!DOCTYPE html>"
                              "<html>"
                              "<head>"
                              "  <title>Twitch Authentication</title>"
                              "  <script>"
                              "    window.onload = function() {"
                              "      var hash = window.location.hash;"
                              "      if (hash) {"
                              "        var params = new URLSearchParams(hash.substring(1));"
                              "        var token = params.get('access_token');"
                              "        if (token) {"
                              "          window.location.href = '/token?access_token=' + token;"
                              "        } else {"
                              "          document.body.innerText = 'Error: Access token not found in URL hash.';"
                              "        }"
                              "      } else {"
                              "        document.body.innerText = 'Error: Hash fragment not found.';"
                              "      }"
                              "    };"
                              "  </script>"
                              "</head>"
                              "<body>"
                              "  <p style='text-align: center; font-family: sans-serif; margin-top: 50px; color: #555;'>"
                              "    Connecting to app... Please wait."
                              "  </p>"
                              "</body>"
                              "</html>";
            socket->write(html);
            socket->disconnectFromHost();
        }
    });
}

void TwitchReader::connectToTwitch() {
    // 短時間に複数回呼ばれても最後の1回だけ実行する（debounce: 300ms）
    if (!m_reconnectTimer) {
        m_reconnectTimer = new QTimer(this);
        m_reconnectTimer->setSingleShot(true);
        connect(m_reconnectTimer, &QTimer::timeout, this, &TwitchReader::doConnectToTwitch);
    }
    // タイマーをリセットして再スタート（連続呼び出しを吸収）
    m_reconnectTimer->start(300);
}

void TwitchReader::doConnectToTwitch() {
    // 既存の接続を安全に破棄
    if (m_webSocket) {
        // シグナルを切断してから close → deleteLater で安全に破棄
        m_webSocket->disconnect(this);
        m_webSocket->close();
        m_webSocket->deleteLater();
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
    // 認証済みトークンがある場合はBotアカウント名（m_botName）で接続、なければ匿名
    QString nick = (!m_oauthToken.isEmpty())
        ? (!m_botName.isEmpty() ? m_botName.toLower() : m_channel.toLower())
        : QStringLiteral("justinfan12345");
    m_webSocket->sendTextMessage("NICK " + nick);
    
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
        qDebug() << "TwitchReader received:" << line;

        if (line.startsWith("PING")) {
            m_webSocket->sendTextMessage("PONG :tmi.twitch.tv");
            continue;
        }

        // 366 = "End of /NAMES list" → JOIN 成功確認，挨拶フラグが立っていれば挨拶発火
        if (line.contains(" 366 ")) {
            if (m_shouldGreet && m_lastGreetedChannel != m_channel) {
                qDebug() << "TwitchReader: JOIN confirmed. Triggering greeting for channel" << m_channel;
                sendGreeting();
            }
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

    // 遅延接続タイマーをキャンセル（停止後に再接続が走らないように）
    if (m_reconnectTimer) {
        m_reconnectTimer->stop();
    }

    if (m_authServer) {
        m_authServer->close();
        m_authServer->deleteLater();
        m_authServer = nullptr;
    }
    if (m_webSocket) {
        m_webSocket->disconnect(this);
        m_webSocket->close();
        m_webSocket->deleteLater();
        m_webSocket = nullptr;
    }
}

void TwitchReader::injectTestComment(const QString &user, const QString &message) {
    // Bot自身や既知のBotアカウントからのメッセージは無視する
    static const QStringList knownBots = {
        "nightbot", "streamelements", "moobot", "streamlabs", "fossabot",
        "wizebot", "phantombot", "coebot", "botisimo", "d0nk"
    };
    QString userLower = user.toLower();
    // Bot自身のアカウント名を除外
    if (!m_botName.isEmpty() && userLower == m_botName.toLower()) {
        qDebug() << "TwitchReader: Ignored self-message from bot account:" << user;
        return;
    }
    // 既知のBotアカウントを除外
    if (knownBots.contains(userLower)) {
        qDebug() << "TwitchReader: Ignored known bot:" << user;
        return;
    }

    qDebug() << "TwitchReader: Injected comment from" << user << ":" << message;

    if (m_wakeWord.isEmpty() && m_avatarName.isEmpty()) return;

    bool isMatch = false;
    QString cleanMessage = message;

    // 1. まずウェイクワードで判定
    if (!m_wakeWord.isEmpty()) {
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
    }

    // 2. ウェイクワードでマッチしなかった場合で、かつ名前で反応が有効な場合、アバター名で判定
    if (!isMatch && m_nameReactionEnabled && !m_avatarName.isEmpty()) {
        if (message.contains(m_avatarName)) {
            isMatch = true;
            cleanMessage = message;
            cleanMessage.replace(m_avatarName, "");
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
        meta["twitch_channel"] = m_channel; // 返信先チャンネルをセット
        event.extraData = meta;

        emit notifyEvent(event);
    }
}

void TwitchReader::saveOAuthDataToSettings(const QString &accessToken, const QString &channel) {
    Q_UNUSED(accessToken);
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
            m_botName = channelName;
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
        if (!channelName.isEmpty()) {
            meta["twitch_username"] = channelName;
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
    QString prevChannel = m_channel;
    loadSettings();
    // チャンネルが変更された場合のみ挨拶フラグを立てる
    if (!m_channel.isEmpty() && m_channel.toLower() != prevChannel.toLower()) {
        qDebug() << "TwitchReader: Channel changed from" << prevChannel << "to" << m_channel << ". Greeting scheduled.";
        m_shouldGreet = true;
    }
    if (m_isRunning) {
        connectToTwitch();
    }
}

void TwitchReader::on_twitchConnectRequested() {
    qDebug() << "TwitchReader: /twitch connect requested. Greeting scheduled.";
    m_shouldGreet = true;
    if (m_isRunning) {
        connectToTwitch();
    } else {
        on_startReading();
    }
}

void TwitchReader::on_twitchReauthRequested() {
    qDebug() << "TwitchReader: Re-authorization requested.";
    on_stopReading();
    
    // トークンをクリア
    m_oauthToken = "";
    
    // UIにトークンがクリアされたことを通知して保存させる
    AppEvent event;
    event.type = EventType::SettingsUpdated;
    event.source = "TwitchReader";
    event.text = "Twitchトークンがクリアされました。";
    QVariantMap meta;
    meta["twitch_oauth_token"] = "";
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

void TwitchReader::on_requestTwitchSend(const QString &channel, const QString &text) {
    if (!m_webSocket || m_webSocket->state() != QAbstractSocket::ConnectedState) {
        qWarning() << "TwitchReader: Cannot send message, not connected.";
        return;
    }
    QString ch = channel.startsWith("#") ? channel : "#" + channel;
    m_webSocket->sendTextMessage(QString("PRIVMSG %1 :%2").arg(ch, text));
    qDebug() << "TwitchReader: Sent PRIVMSG to" << ch << ":" << text;
}

void TwitchReader::sendGreeting() {
    m_shouldGreet = false;
    m_lastGreetedChannel = m_channel;

    // 挨拶プロンプトを TwitchCommentReceived として発火→ AI が自然な挨拶文を生成してチャンネルへ送信する
    AppEvent event;
    event.type = EventType::TwitchCommentReceived;
    event.source = "TwitchReader";
    event.text   = "\uff08システム）チャンネルに接続しました。視聴者に明るく挨拶してください。";

    QVariantMap meta;
    meta["user"]         = "__system_greeting__";
    meta["raw_message"]  = "";
    meta["twitch_channel"] = m_channel;
    meta["is_greeting"]  = true;
    event.extraData = meta;

    qDebug() << "TwitchReader: Emitting greeting event for channel" << m_channel;
    emit notifyEvent(event);
}
