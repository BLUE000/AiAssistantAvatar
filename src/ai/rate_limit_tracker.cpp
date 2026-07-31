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
}

// ---------------------------------------------------------------------------
// updateFromReply — OpenAI互換レートリミットヘッダーを解析して残量を更新
// ---------------------------------------------------------------------------
void RateLimitTracker::updateFromReply(const QString &clientId, QNetworkReply *reply) {
    if (!reply || !m_statuses.contains(clientId)) return;
    ProviderStatus &s = m_statuses[clientId];

    auto getInt = [&](const QByteArray &key, int fallback) -> int {
        QByteArray v = reply->rawHeader(key).trimmed();
        if (v.isEmpty()) return fallback;
        bool ok = false;
        int val = v.toInt(&ok);
        return ok ? val : fallback;
    };
    auto getDate = [&](const QByteArray &key) -> QDateTime {
        QByteArray v = reply->rawHeader(key).trimmed();
        if (v.isEmpty()) return QDateTime();
        return parseResetHeader(v);
    };

    int newRpmMax = getInt("x-ratelimit-limit-requests", 0);
    if (newRpmMax > 0) s.rpmMax = newRpmMax;

    int newRpm = getInt("x-ratelimit-remaining-requests", -1);
    if (newRpm >= 0) s.rpmRemaining = newRpm;

    int newTpmMax = getInt("x-ratelimit-limit-tokens", 0);
    if (newTpmMax > 0) s.tpmMax = newTpmMax;

    int newTpm = getInt("x-ratelimit-remaining-tokens", -1);
    if (newTpm >= 0) s.tpmRemaining = newTpm;

    QDateTime resetAt = getDate("x-ratelimit-reset-requests");
    if (resetAt.isValid()) s.nextResetAt = resetAt;

    updateAvailable(clientId);
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
    return m_statuses.value(clientId, ProviderStatus{});
}

QList<ProviderStatus> RateLimitTracker::allStatuses() const {
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
    ProviderStatus &s = m_statuses[clientId];
    bool rpmOk = (s.rpmMax <= 0) || (s.rpmRemaining > 0);
    bool rpdOk = (s.rpdMax <= 0) || (s.rpdRemaining > 0);
    s.available = rpmOk && rpdOk;
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
