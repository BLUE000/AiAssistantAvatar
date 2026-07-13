#pragma once
#include "provider_status.h"
#include <QMap>
#include <QList>
#include <QString>
#include <QDateTime>

class QNetworkReply;

/// 各AIクライアントのレートリミット使用量を追跡・管理するクラス
class RateLimitTracker {
public:
    struct ResetInfo {
        QDateTime resetAt;
        QString   clientId;
        QString   limitType; // "RPM" / "RPD" / "TPM" など
    };

    /// 起動時に全クライアントのデフォルト状態を登録する
    void registerClient(const ProviderStatus &defaultStatus);

    /// APIレスポンスヘッダーから残量を更新する（毎APIコール後に呼ぶ）
    void updateFromReply(const QString &clientId, QNetworkReply *reply);

    /// レートリミットエラー検出時に、強制的に使用不可にする
    void forceRateLimit(const QString &clientId, int durationSecs);

    /// 手動設定値でMax値を上書きする（UI設定反映用）
    void setMaxValues(const QString &clientId, const ProviderStatus &manual);

    /// レイテンシを移動平均（直近5回）で更新する
    void recordLatency(const QString &clientId, int elapsedMs);

    /// 使用可能かチェック（rpmRemaining > 0 かつ rpdRemaining > 0、またはMax未設定）
    bool isAvailable(const QString &clientId) const;

    /// 全クライアントが枯渇している場合、最短リセット時刻情報を返す
    ResetInfo earliestResetTime() const;

    /// 「X分後に使用可能」メッセージを生成（AI呼び出しなし）
    QString formatWaitMessage(const ResetInfo &info) const;

    /// 現在の ProviderStatus を取得（UI表示用）
    ProviderStatus statusOf(const QString &clientId) const;
    QList<ProviderStatus> allStatuses() const;
    QStringList registeredClientIds() const;

    /// 日/週/月単位の使用量を永続化・読み込み
    void saveToFile(const QString &path) const;
    void loadFromFile(const QString &path);

private:
    QMap<QString, ProviderStatus> m_statuses;
    QMap<QString, QList<int>>     m_latencyHistory; // 直近5回

    void updateAvailable(const QString &clientId);
    static QDateTime parseResetHeader(const QByteArray &value);
};
