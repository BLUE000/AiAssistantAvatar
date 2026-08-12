#include "rate_limit_tracker.h"
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDebug>
#include <algorithm>

// ---------------------------------------------------------------------------
// registerClient
// ---------------------------------------------------------------------------
void RateLimitTracker::registerClient(const ProviderStatus &defaultStatus) {
    m_statuses[defaultStatus.provider] = defaultStatus;
    m_latencyHistory[defaultStatus.provider] = {};
    if (!m_safetyMargins.contains(defaultStatus.provider)) {
        m_safetyMargins[defaultStatus.provider] = 1.2; // 初期安全マージン係数 α
    }
}

// ---------------------------------------------------------------------------
// updateFromReply — 各プロバイダのヘッダー表記揺れに対応して残量を更新
// ---------------------------------------------------------------------------
void RateLimitTracker::updateFromReply(const QString &clientId, QNetworkReply *reply) {
    if (!reply || !m_statuses.contains(clientId)) return;
    ProviderStatus &s = m_statuses[clientId];

    auto getIntMulti = [&](const QList<QByteArray> &keys, int fallback) -> int {
        for (const auto &key : keys) {
            QByteArray v = reply->rawHeader(key).trimmed();
            if (!v.isEmpty()) {
                bool ok = false;
                int val = v.toInt(&ok);
                if (ok) return val;
            }
        }
        return fallback;
    };
    auto getDateMulti = [&](const QList<QByteArray> &keys) -> QDateTime {
        for (const auto &key : keys) {
            QByteArray v = reply->rawHeader(key).trimmed();
            if (!v.isEmpty()) {
                QDateTime dt = parseResetHeader(v);
                if (dt.isValid()) return dt;
            }
        }
        return QDateTime();
    };

    int newRpmMax = getIntMulti({"x-ratelimit-limit-requests", "x-ratelimit-limit-requests-minute", "x-ratelimit-limit"}, 0);
    if (newRpmMax > 0) s.rpmMax = newRpmMax;

    int newRpm = getIntMulti({"x-ratelimit-remaining-requests", "x-ratelimit-remaining-requests-minute", "x-ratelimit-remaining"}, -1);
    if (newRpm >= 0) s.rpmRemaining = newRpm;

    int newTpmMax = getIntMulti({"x-ratelimit-limit-tokens", "x-ratelimit-limit-tokens-minute"}, 0);
    if (newTpmMax > 0) s.tpmMax = newTpmMax;

    int newTpm = getIntMulti({"x-ratelimit-remaining-tokens", "x-ratelimit-remaining-tokens-minute"}, -1);
    if (newTpm >= 0) s.tpmRemaining = newTpm;

    QDateTime resetAt = getDateMulti({"x-ratelimit-reset-requests", "x-ratelimit-reset-requests-minute", "x-ratelimit-reset"});
    if (resetAt.isValid()) s.nextResetAt = resetAt;

    if (newRpm >= 0 || newTpm >= 0) {
        calibrateFromHeader(clientId, newRpm, newTpm);
    }

    updateAvailable(clientId);
}

// ---------------------------------------------------------------------------
// recordLocalConsumption — ヘッダー未取得時のローカルカウントダウン減算
// ---------------------------------------------------------------------------
void RateLimitTracker::recordLocalConsumption(const QString &clientId, int inputLength, int outputLength) {
    if (!m_statuses.contains(clientId)) return;
    ProviderStatus &s = m_statuses[clientId];

    double alpha = m_safetyMargins.value(clientId, 1.2);

    // リクエスト件数の減算
    if (s.rpmMax > 0 && s.rpmRemaining > 0) {
        s.rpmRemaining = qMax(0, s.rpmRemaining - 1);
    }
    if (s.rpdMax > 0 && s.rpdRemaining > 0) {
        s.rpdRemaining = qMax(0, s.rpdRemaining - 1);
    }

    // 初回減算時に 1 分後のリセットタイマーが未設定なら設定
    if ((s.rpmMax > 0 && s.rpmRemaining < s.rpmMax) || (s.tpmMax > 0 && s.tpmRemaining < s.tpmMax)) {
        if (!s.nextResetAt.isValid()) {
            s.nextResetAt = QDateTime::currentDateTimeUtc().addSecs(60);
        }
    }


    // 文字列長からトークン数推定 (1文字 ≒ 1.3 トークン)
    int estTokens = qMax(1, static_cast<int>(std::ceil((inputLength * 1.3 + outputLength * 1.3) * alpha)));

    if (s.tpmMax > 0 && s.tpmRemaining > 0) {
        s.tpmRemaining = qMax(0, s.tpmRemaining - estTokens);
    }
    if (s.tpdMax > 0 && s.tpdRemaining > 0) {
        s.tpdRemaining = qMax(0, s.tpdRemaining - estTokens);
    }

    updateAvailable(clientId);
}

// ---------------------------------------------------------------------------
// adaptOnHttp429 — 429超過検知時の学習校正
// ---------------------------------------------------------------------------
void RateLimitTracker::adaptOnHttp429(const QString &clientId, int durationSecs) {
    if (!m_statuses.contains(clientId)) return;
    forceRateLimit(clientId, durationSecs);

    double currentAlpha = m_safetyMargins.value(clientId, 1.2);
    double newAlpha = qMin(3.0, currentAlpha * 1.25);
    m_safetyMargins[clientId] = newAlpha;

    qWarning() << "[RateLimitTracker] 429 Limit Exceeded on" << clientId
               << ". Adapted safety margin alpha from" << currentAlpha << "to" << newAlpha;
}

// ---------------------------------------------------------------------------
// calibrateFromHeader — ヘッダー実測値による指数移動平均 (EMA) 学習校正
// ---------------------------------------------------------------------------
void RateLimitTracker::calibrateFromHeader(const QString &clientId, int actualRemainingRpm, int actualRemainingTpm) {
    if (!m_statuses.contains(clientId)) return;
    Q_UNUSED(actualRemainingRpm);
    Q_UNUSED(actualRemainingTpm);

    // 実測値ヘッダーが取れる場合は安定運用中とみなし、安全係数を緩やかに標準値 1.2 へ収束させる
    double alpha = m_safetyMargins.value(clientId, 1.2);
    double targetAlpha = 1.2;
    m_safetyMargins[clientId] = alpha * 0.9 + targetAlpha * 0.1;
}

// ---------------------------------------------------------------------------
// safetyMargin
// ---------------------------------------------------------------------------
double RateLimitTracker::safetyMargin(const QString &clientId) const {
    return m_safetyMargins.value(clientId, 1.2);
}

// ---------------------------------------------------------------------------
// forceRateLimit
// ---------------------------------------------------------------------------
void RateLimitTracker::forceRateLimit(const QString &clientId, int durationSecs) {
    if (!m_statuses.contains(clientId)) return;
    ProviderStatus &s = m_statuses[clientId];
    s.rpmRemaining = 0;
    s.nextResetAt = QDateTime::currentDateTimeUtc().addSecs(durationSecs);
    s.available = false;
    qWarning() << "[RateLimitTracker] Forced rate limit on client" << clientId << "for" << durationSecs << "seconds.";
}


// ---------------------------------------------------------------------------
// setMaxValues
// ---------------------------------------------------------------------------
void RateLimitTracker::setMaxValues(const QString &clientId, const ProviderStatus &manual) {
    if (!m_statuses.contains(clientId)) return;
    ProviderStatus &s = m_statuses[clientId];
    if (manual.rpmMax > 0) { s.rpmMax = manual.rpmMax; s.rpmRemaining = qMin(s.rpmRemaining, s.rpmMax); }
    if (manual.rpdMax > 0) { s.rpdMax = manual.rpdMax; s.rpdRemaining = qMin(s.rpdRemaining, s.rpdMax); }
    if (manual.tpmMax > 0) { s.tpmMax = manual.tpmMax; s.tpmRemaining = qMin(s.tpmRemaining, s.tpmMax); }
    if (manual.tpdMax > 0) { s.tpdMax = manual.tpdMax; s.tpdRemaining = qMin(s.tpdRemaining, s.tpdMax); }
    if (manual.contextWindow > 0) s.contextWindow = manual.contextWindow;
    s.toolCall     = manual.toolCall;
    s.supportsDiff = manual.supportsDiff;
    s.cost         = manual.cost;
    updateAvailable(clientId);
}

// ---------------------------------------------------------------------------
// recordLatency — 直近5回の移動平均
// ---------------------------------------------------------------------------
void RateLimitTracker::recordLatency(const QString &clientId, int elapsedMs) {
    if (!m_statuses.contains(clientId)) return;
    QList<int> &hist = m_latencyHistory[clientId];
    hist.append(elapsedMs);
    if (hist.size() > 5) hist.removeFirst();
    int sum = 0;
    for (int v : hist) sum += v;
    m_statuses[clientId].latencyMs = sum / hist.size();
}

// ---------------------------------------------------------------------------
// isAvailable
// ---------------------------------------------------------------------------
bool RateLimitTracker::isAvailable(const QString &clientId) const {
    if (!m_statuses.contains(clientId)) return false;
    const_cast<RateLimitTracker*>(this)->updateAvailable(clientId);
    return m_statuses[clientId].available;
}


// ---------------------------------------------------------------------------
// earliestResetTime
// ---------------------------------------------------------------------------
RateLimitTracker::ResetInfo RateLimitTracker::earliestResetTime() const {
    ResetInfo best;
    for (auto it = m_statuses.constBegin(); it != m_statuses.constEnd(); ++it) {
        const ProviderStatus &s = it.value();
        if (s.nextResetAt.isValid()) {
            if (!best.resetAt.isValid() || s.nextResetAt < best.resetAt) {
                best.resetAt  = s.nextResetAt;
                best.clientId = it.key();
                best.limitType = (s.rpmRemaining <= 0) ? "RPM" : "RPD";
            }
        }
    }
    // nextResetAt が設定されていなかった場合のフォールバック（1分後）
    if (!best.resetAt.isValid()) {
        best.resetAt  = QDateTime::currentDateTimeUtc().addSecs(60);
        best.clientId = m_statuses.isEmpty() ? "" : m_statuses.firstKey();
        best.limitType = "RPM";
    }
    return best;
}

// ---------------------------------------------------------------------------
// formatWaitMessage
// ---------------------------------------------------------------------------
QString RateLimitTracker::formatWaitMessage(const ResetInfo &info) const {
    QString timeStr;
    qint64 secsLeft = QDateTime::currentDateTimeUtc().secsTo(info.resetAt);
    if (secsLeft <= 0) {
        timeStr = "まもなく";
    } else if (secsLeft < 60) {
        timeStr = QString("%1秒後").arg(secsLeft);
    } else if (secsLeft < 3600) {
        timeStr = QString("%1分後").arg((secsLeft + 59) / 60);
    } else {
        timeStr = info.resetAt.toLocalTime().toString("本日HH:MM");
    }

    QString providerDisplay = info.clientId;
    if (providerDisplay == "groq")     providerDisplay = "Groq";
    else if (providerDisplay == "cerebras") providerDisplay = "Cerebras";
    else if (providerDisplay == "mistral")  providerDisplay = "Mistral";

    return QString("現在すべてのAIクライアントがレート制限に達しています。"
                   "最短で%1に使用可能になります（%2 %3制限解除）。")
           .arg(timeStr, providerDisplay, info.limitType);
}

// ---------------------------------------------------------------------------
// statusOf / allStatuses / registeredClientIds
// ---------------------------------------------------------------------------
ProviderStatus RateLimitTracker::statusOf(const QString &clientId) const {
    const_cast<RateLimitTracker*>(this)->updateAvailable(clientId);
    return m_statuses.value(clientId, ProviderStatus{});
}

QList<ProviderStatus> RateLimitTracker::allStatuses() const {
    auto *self = const_cast<RateLimitTracker*>(this);
    for (const QString &id : m_statuses.keys()) {
        self->updateAvailable(id);
    }
    return m_statuses.values();
}

QStringList RateLimitTracker::registeredClientIds() const {
    return m_statuses.keys();
}

// ---------------------------------------------------------------------------
// saveToFile / loadFromFile — 日単位の永続化
// ---------------------------------------------------------------------------
void RateLimitTracker::saveToFile(const QString &path) const {
    QJsonObject root;
    QString today = QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddT00:00:00Z");
    for (auto it = m_statuses.constBegin(); it != m_statuses.constEnd(); ++it) {
        const ProviderStatus &s = it.value();
        QJsonObject obj;
        obj["rpd_max"]       = s.rpdMax;
        obj["rpd_remaining"] = s.rpdRemaining;
        obj["tpd_max"]       = s.tpdMax;
        obj["tpd_remaining"] = s.tpdRemaining;
        obj["day_start"]     = today;
        obj["safety_margin"] = m_safetyMargins.value(it.key(), 1.2);
        root[it.key()] = obj;
    }
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }
}

void RateLimitTracker::loadFromFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return;

    QJsonObject root = doc.object();
    QString today = QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd");

    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        QString id = it.key();
        if (!m_statuses.contains(id)) continue;
        QJsonObject obj = it.value().toObject();

        if (obj.contains("safety_margin")) {
            m_safetyMargins[id] = obj["safety_margin"].toDouble(1.2);
        }

        QString dayStart = obj["day_start"].toString();
        bool sameDay = dayStart.startsWith(today);

        ProviderStatus &s = m_statuses[id];
        if (sameDay) {
            if (obj.contains("rpd_remaining")) s.rpdRemaining = obj["rpd_remaining"].toInt();
            if (obj.contains("tpd_remaining")) s.tpdRemaining = obj["tpd_remaining"].toInt();
        } else {
            // 日が変わったのでデフォルトにリセット（rpdMax を残量に戻す）
            s.rpdRemaining = s.rpdMax;
            s.tpdRemaining = s.tpdMax;
            qDebug() << "RateLimitTracker: day changed, reset daily counters for" << id;
        }
        if (obj.contains("rpd_max") && obj["rpd_max"].toInt() > 0)
            s.rpdMax = obj["rpd_max"].toInt();
        if (obj.contains("tpd_max") && obj["tpd_max"].toInt() > 0)
            s.tpdMax = obj["tpd_max"].toInt();

        updateAvailable(id);
    }
}


// ---------------------------------------------------------------------------
// private helpers
// ---------------------------------------------------------------------------
void RateLimitTracker::updateAvailable(const QString &clientId) {
    if (!m_statuses.contains(clientId)) return;
    ProviderStatus &s = m_statuses[clientId];
    QDateTime nowUtc = QDateTime::currentDateTimeUtc();

    // 1. nextResetAt が期限超過した場合は使用枠を全量自動再補充し、リセット状態を完了
    if (s.nextResetAt.isValid() && s.nextResetAt <= nowUtc) {
        if (s.rpmMax > 0) s.rpmRemaining = s.rpmMax;
        else if (s.rpmMax <= 0) s.rpmRemaining = -1;

        if (s.tpmMax > 0) s.tpmRemaining = s.tpmMax;
        else if (s.tpmMax <= 0) s.tpmRemaining = -1;

        if (s.rpdMax > 0) s.rpdRemaining = s.rpdMax;
        else if (s.rpdMax <= 0) s.rpdRemaining = -1;

        s.nextResetAt = QDateTime();
        s.available = true;
    }

    // 2. 数値制限のある項目のチェック
    bool rpmOk = (s.rpmMax <= 0) || (s.rpmRemaining == -1) || (s.rpmRemaining > 0);
    bool rpdOk = (s.rpdMax <= 0) || (s.rpdRemaining == -1) || (s.rpdRemaining > 0);
    bool tpmOk = (s.tpmMax <= 0) || (s.tpmRemaining == -1) || (s.tpmRemaining > 0);
    bool tpdOk = (s.tpdMax <= 0) || (s.tpdRemaining == -1) || (s.tpdRemaining > 0);

    if (!rpmOk || !rpdOk || !tpmOk || !tpdOk) {
        s.available = false;
    } else if (s.nextResetAt.isValid() && (s.rpmRemaining == 0 || s.rpdRemaining == 0)) {
        s.available = false;
    }
}


QString RateLimitTracker::selectBestAvailableClient() const {
    QString bestClient;
    int maxRemaining = -1;

    for (auto it = m_statuses.constBegin(); it != m_statuses.constEnd(); ++it) {
        const QString &id = it.key();
        const ProviderStatus &s = it.value();
        if (id == "dummy") continue;

        if (s.available) {
            int currentQuota = (s.rpmRemaining > 0) ? s.rpmRemaining : ((s.rpdRemaining > 0) ? s.rpdRemaining : 9999);
            if (currentQuota > maxRemaining) {
                maxRemaining = currentQuota;
                bestClient = id;
            }
        }
    }
    return bestClient;
}

QDateTime RateLimitTracker::parseResetHeader(const QByteArray &value) {
    // ISO 8601 形式: "2026-07-12T12:34:56Z"
    QDateTime dt = QDateTime::fromString(QString(value), Qt::ISODate);
    if (dt.isValid()) return dt;

    // "Xs" 形式（秒数）: "42s" → 現在時刻 + 42秒
    QString s = QString(value).trimmed();
    if (s.endsWith('s')) {
        bool ok = false;
        int secs = s.chopped(1).toInt(&ok);
        if (ok) return QDateTime::currentDateTimeUtc().addSecs(secs);
    }

    // 数値のみ（秒数）
    bool ok = false;
    int secs = QString(value).toInt(&ok);
    if (ok) return QDateTime::currentDateTimeUtc().addSecs(secs);

    return QDateTime();
}
