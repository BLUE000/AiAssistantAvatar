#ifndef COMMUNITY_OBSERVER_ENGINE_H
#define COMMUNITY_OBSERVER_ENGINE_H

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

enum class ObserverStatus {
    Normal,             // 通常状態（普段通り・問題なし）
    DrasticChange,      // 乖離（普段と大きく異なる強い感情・棘・否定・投げやり）
    PersistentConcern   // 継続的懸念（過去ログに同一対象への不満・敵意が継続）
};

struct ObserverEvaluationResult {
    ObserverStatus status = ObserverStatus::Normal;
    QString statusString = "Normal";
    QString user;
    QString platform = "twitch";
    int historyCount = 0;
    QString usualTopics;
    QString usualSentiment;
    QString currentTopic;
    QString currentSentiment;
    int concernLevel = 0; // 0: なし, 1: 軽度, 2: 中度 (乖離), 3: 重度 (継続)
    QString anomalySummary;
    QString directive; // Worker AI への傾聴・対話誘導プロンプト指示

    QJsonObject toJson() const;
    static ObserverEvaluationResult fromJson(const QJsonObject &obj);
};

struct UserMessageRecord {
    QDateTime timestamp;
    QString text;
    QString topic;
    QString tone;

    QJsonObject toJson() const;
    static UserMessageRecord fromJson(const QJsonObject &obj);
};

class CommunityObserverEngine {
public:
    CommunityObserverEngine();
    explicit CommunityObserverEngine(const QString &baseDir);

    // ユーザー発言ログの記録
    bool recordMessage(const QString &platform, const QString &user, const QString &text);

    // ユーザー発言の違和感・傾向評価
    ObserverEvaluationResult evaluateMessage(const QString &platform, const QString &user, const QString &text);

    // 記録と評価を同時に実行
    ObserverEvaluationResult recordAndEvaluate(const QString &platform, const QString &user, const QString &text);

    // ユーザーログのローテーション・クリーンアップ（指定日数以前を削除、最大件数100件維持）
    int vacuumLogs(int maxDays = 60, int maxRecordsPerUser = 100);

    // 指定ユーザーの蓄積ログサマリ取得（管理・デバッグ用）
    QJsonObject inspectUser(const QString &platform, const QString &user);

    // ログ保存ディレクトリの設定・取得
    void setLogsDirectory(const QString &dirPath);
    QString logsDirectory() const;

    // トピックおよびトーンの客観的分類ヘルパー
    static QString classifyTopic(const QString &text);
    static QString classifyTone(const QString &text);

private:
    QString m_logsDirectory;

    QString resolveLogFilePath(const QString &platform, const QString &user) const;
    QList<UserMessageRecord> loadUserRecords(const QString &filePath) const;
    bool saveUserRecords(const QString &filePath, const QString &platform, const QString &user, const QList<UserMessageRecord> &records) const;
};

#endif // COMMUNITY_OBSERVER_ENGINE_H
