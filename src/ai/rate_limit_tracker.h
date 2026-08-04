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

    /// 最も利用残枠 (RPM/RPD) が多いクライアント名を取得する
    QString selectBestAvailableClient() const;

    /// ローカル内部でリクエスト・トークン消費量を減算更新する（ヘッダー非対応時/フォールバック用）
    void recordLocalConsumption(const QString &clientId, int inputLength, int outputLength);

    /// HTTP 429 エラー発生時に自律適応学習を行い、安全マージン係数を上方修正する
    void adaptOnHttp429(const QString &clientId, int durationSecs = 60);

    /// ヘッダー実測値による学習校正（EMA指数移動平均による安全係数の微調整）
    void calibrateFromHeader(const QString &clientId, int actualRemainingRpm, int actualRemainingTpm);

    /// クライアントごとの現在の学習済み安全マージン係数 α を取得
    double safetyMargin(const QString &clientId) const;

    /// 日/週/月単位の使用量を永続化・読み込み
    void saveToFile(const QString &path) const;
    void loadFromFile(const QString &path);

private:
    QMap<QString, ProviderStatus> m_statuses;
    QMap<QString, QList<int>>     m_latencyHistory; // 直近5回
    QMap<QString, double>         m_safetyMargins;  // クライアント毎の安全マージン係数 α


    void updateAvailable(const QString &clientId);
    static QDateTime parseResetHeader(const QByteArray &value);
};
