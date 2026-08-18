#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

struct CreatorHelixInfo {
    QString userId;
    QString login;
    QString displayName;
    QString description; // Bio
    QString gameName;    // 配信カテゴリ（現在）
    QString title;       // 配信タイトル
    QString snsInfo;     // 抽出された公式Twitter/YouTube/TikTok等のURL
    QStringList recentGames; // 最近の配信ゲーム履歴（重複除去・最大5件）
};

class TwitchHelixClient : public QObject {
    Q_OBJECT
private:
    QNetworkAccessManager *m_networkManager;
    QString m_oauthToken;
    QString m_clientId;

public:
    explicit TwitchHelixClient(QObject *parent = nullptr);
    void setCredentials(const QString &oauthToken, const QString &clientId);

    // 非同期で Helix API からクリエイター情報を取得
    void fetchCreatorInfo(const QString &username, std::function<void(const CreatorHelixInfo &info, bool success)> callback);

    // 非同期で Twitch Helix API (POST /helix/chat/announcements) から公式カラーアナウンスバナーを発信
    void sendChatAnnouncement(const QString &broadcasterId, const QString &moderatorId, const QString &message, const QString &color = "primary", std::function<void(bool success)> callback = nullptr);

    // 非同期で Twitch Helix API (POST /helix/chat/shoutouts) から公式 Shoutout リクエストを発信
    void sendShoutout(const QString &fromBroadcasterId, const QString &toBroadcasterId, const QString &moderatorId, std::function<void(bool success)> callback = nullptr);
    void sendShoutoutToUser(const QString &fromUsername, const QString &toUsername, std::function<void(bool success)> callback = nullptr);

    // Bio から SNS・外部リンクを抽出する純粋関数
    static QString extractSnsInfo(const QString &bio);
};
