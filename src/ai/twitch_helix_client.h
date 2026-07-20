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
    QString gameName;    // 配信カテゴリ
    QString title;       // 配信タイトル
    QString snsInfo;     // 抽出された公式Twitter/YouTube URL
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

private:
    QString extractSnsInfo(const QString &bio) const;
};
