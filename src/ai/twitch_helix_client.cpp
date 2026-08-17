#include "twitch_helix_client.h"
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QDebug>

TwitchHelixClient::TwitchHelixClient(QObject *parent)
    : QObject(parent), m_networkManager(new QNetworkAccessManager(this)) {}

void TwitchHelixClient::setCredentials(const QString &oauthToken, const QString &clientId) {
    m_oauthToken = oauthToken;
    m_clientId = clientId;
}

QString TwitchHelixClient::extractSnsInfo(const QString &bio) const {
    if (bio.isEmpty()) return "";
    
    QStringList foundUrls;
    // Twitter(X) / YouTube / TikTok / Instagram / Discord / Linktree
    QRegularExpression regex(QStringLiteral(
        "https?:\\/\\/(www\\.)?"
        "(twitter\\.com|x\\.com|youtube\\.com|youtu\\.be"
        "|tiktok\\.com|instagram\\.com|discord\\.gg|linktr\\.ee)"
        "\/[a-zA-Z0-9_@.\\-]+"
    ));
    QRegularExpressionMatchIterator i = regex.globalMatch(bio);
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        foundUrls.append(match.captured(0));
    }
    
    return foundUrls.join(", ");
}

void TwitchHelixClient::fetchCreatorInfo(const QString &username, std::function<void(const CreatorHelixInfo &info, bool success)> callback) {
    if (username.isEmpty() || m_clientId.isEmpty()) {
        qWarning() << "TwitchHelixClient: Missing username or Client ID.";
        if (callback) callback(CreatorHelixInfo(), false);
        return;
    }

    // 1. GET /helix/users
    QUrl userUrl("https://api.twitch.tv/helix/users");
    QUrlQuery userQuery;
    userQuery.addQueryItem("login", username.toLower());
    userUrl.setQuery(userQuery);

    QNetworkRequest userReq(userUrl);
    userReq.setRawHeader("Client-ID", m_clientId.toUtf8());
    if (!m_oauthToken.isEmpty()) {
        userReq.setRawHeader("Authorization", ("Bearer " + m_oauthToken).toUtf8());
    }

    QNetworkReply *userReply = m_networkManager->get(userReq);
    connect(userReply, &QNetworkReply::finished, this, [this, userReply, callback]() {
        userReply->deleteLater();
        if (userReply->error() != QNetworkReply::NoError) {
            qWarning() << "TwitchHelixClient: Users API error:" << userReply->errorString();
            if (callback) callback(CreatorHelixInfo(), false);
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(userReply->readAll());
        QJsonObject obj = doc.object();
        QJsonArray data = obj["data"].toArray();
        if (data.isEmpty()) {
            qWarning() << "TwitchHelixClient: User not found.";
            if (callback) callback(CreatorHelixInfo(), false);
            return;
        }

        QJsonObject userObj = data.at(0).toObject();
        CreatorHelixInfo info;
        info.userId = userObj["id"].toString();
        info.login = userObj["login"].toString();
        info.displayName = userObj["display_name"].toString();
        info.description = userObj["description"].toString();
        info.snsInfo = extractSnsInfo(info.description);

        // 2. GET /helix/channels
        QUrl channelUrl("https://api.twitch.tv/helix/channels");
        QUrlQuery channelQuery;
        channelQuery.addQueryItem("broadcaster_id", info.userId);
        channelUrl.setQuery(channelQuery);

        QNetworkRequest channelReq(channelUrl);
        channelReq.setRawHeader("Client-ID", m_clientId.toUtf8());
        if (!m_oauthToken.isEmpty()) {
            channelReq.setRawHeader("Authorization", ("Bearer " + m_oauthToken).toUtf8());
        }

        QNetworkReply *channelReply = m_networkManager->get(channelReq);
        connect(channelReply, &QNetworkReply::finished, this, [this, channelReply, info, callback]() mutable {
            channelReply->deleteLater();
            if (channelReply->error() == QNetworkReply::NoError) {
                QJsonDocument cDoc = QJsonDocument::fromJson(channelReply->readAll());
                QJsonArray cData = cDoc.object()["data"].toArray();
                if (!cData.isEmpty()) {
                    QJsonObject cObj = cData.at(0).toObject();
                    info.gameName = cObj["game_name"].toString();
                    info.title    = cObj["title"].toString();
                }
            }

            // 3. GET /helix/videos?user_id=...&type=archive (最近の配信ゲーム履歴取得)
            QUrl videoUrl("https://api.twitch.tv/helix/videos");
            QUrlQuery videoQuery;
            videoQuery.addQueryItem("user_id", info.userId);
            videoQuery.addQueryItem("type", "archive");
            videoQuery.addQueryItem("first", "20"); // 重複除去後5件を確保するため多めに取得
            videoUrl.setQuery(videoQuery);

            QNetworkRequest videoReq(videoUrl);
            videoReq.setRawHeader("Client-ID", m_clientId.toUtf8());
            if (!m_oauthToken.isEmpty()) {
                videoReq.setRawHeader("Authorization", ("Bearer " + m_oauthToken).toUtf8());
            }

            QNetworkReply *videoReply = m_networkManager->get(videoReq);
            connect(videoReply, &QNetworkReply::finished, this, [videoReply, info, callback]() mutable {
                videoReply->deleteLater();
                if (videoReply->error() == QNetworkReply::NoError) {
                    QJsonDocument vDoc = QJsonDocument::fromJson(videoReply->readAll());
                    QJsonArray vData = vDoc.object()["data"].toArray();
                    QStringList games;
                    for (const QJsonValue &v : vData) {
                        QString g = v.toObject()["game_name"].toString().trimmed();
                        if (!g.isEmpty() && !games.contains(g)) {
                            games.append(g);
                            if (games.size() >= 5) break;
                        }
                    }
                    info.recentGames = games;
                }
                if (callback) callback(info, true);
            });
        });
    });
}

void TwitchHelixClient::sendChatAnnouncement(const QString &broadcasterId, const QString &moderatorId, const QString &message, const QString &color, std::function<void(bool success)> callback) {
    if (broadcasterId.isEmpty() || moderatorId.isEmpty() || m_clientId.isEmpty() || m_oauthToken.isEmpty()) {
        qWarning() << "TwitchHelixClient: Missing parameters or credentials for announcement.";
        if (callback) callback(false);
        return;
    }

    QUrl url("https://api.twitch.tv/helix/chat/announcements");
    QUrlQuery query;
    query.addQueryItem("broadcaster_id", broadcasterId);
    query.addQueryItem("moderator_id", moderatorId);
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Client-ID", m_clientId.toUtf8());
    req.setRawHeader("Authorization", ("Bearer " + m_oauthToken).toUtf8());

    QJsonObject bodyObj;
    bodyObj["message"] = message;
    bodyObj["color"] = color;

    QNetworkReply *reply = m_networkManager->post(req, QJsonDocument(bodyObj).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        reply->deleteLater();
        bool ok = (reply->error() == QNetworkReply::NoError);
        if (!ok) {
            qWarning() << "TwitchHelixClient: Chat announcement API error:" << reply->errorString();
        } else {
            qDebug() << "TwitchHelixClient: Chat announcement sent successfully via Helix API.";
        }
        if (callback) callback(ok);
    });
}

void TwitchHelixClient::sendShoutout(const QString &fromBroadcasterId, const QString &toBroadcasterId, const QString &moderatorId, std::function<void(bool success)> callback) {
    if (fromBroadcasterId.isEmpty() || toBroadcasterId.isEmpty() || moderatorId.isEmpty() || m_clientId.isEmpty() || m_oauthToken.isEmpty()) {
        qWarning() << "TwitchHelixClient: Missing parameters or credentials for shoutout.";
        if (callback) callback(false);
        return;
    }

    QUrl url("https://api.twitch.tv/helix/chat/shoutouts");
    QUrlQuery query;
    query.addQueryItem("from_broadcaster_id", fromBroadcasterId);
    query.addQueryItem("to_broadcaster_id", toBroadcasterId);
    query.addQueryItem("moderator_id", moderatorId);
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Client-ID", m_clientId.toUtf8());
    req.setRawHeader("Authorization", ("Bearer " + m_oauthToken).toUtf8());

    QNetworkReply *reply = m_networkManager->post(req, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        reply->deleteLater();
        bool ok = (reply->error() == QNetworkReply::NoError);
        if (!ok) {
            qWarning() << "TwitchHelixClient: Shoutout API error:" << reply->errorString();
        } else {
            qDebug() << "TwitchHelixClient: Shoutout sent successfully via Helix API.";
        }
        if (callback) callback(ok);
    });
}

void TwitchHelixClient::sendShoutoutToUser(const QString &fromUsername, const QString &toUsername, std::function<void(bool success)> callback) {
    if (fromUsername.isEmpty() || toUsername.isEmpty()) {
        if (callback) callback(false);
        return;
    }

    fetchCreatorInfo(fromUsername, [this, fromUsername, toUsername, callback](const CreatorHelixInfo &fromInfo, bool fromSuccess) {
        if (!fromSuccess || fromInfo.userId.isEmpty()) {
            qWarning() << "TwitchHelixClient: Failed to fetch fromBroadcasterId for" << fromUsername;
            if (callback) callback(false);
            return;
        }

        fetchCreatorInfo(toUsername, [this, fromInfo, toUsername, callback](const CreatorHelixInfo &toInfo, bool toSuccess) {
            if (!toSuccess || toInfo.userId.isEmpty()) {
                qWarning() << "TwitchHelixClient: Failed to fetch toBroadcasterId for" << toUsername;
                if (callback) callback(false);
                return;
            }

            sendShoutout(fromInfo.userId, toInfo.userId, fromInfo.userId, callback);
        });
    });
}
