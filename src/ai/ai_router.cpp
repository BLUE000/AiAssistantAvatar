#include "ai_router.h"
#include "rate_limit_tracker.h"

QString AIRouter::selectClient(AIRole role,
                               const RateLimitTracker &tracker,
                               const QStringList &priorityOrder) const {
    Q_UNUSED(role) // 現フェーズは Worker / Manager とも同一ロジック

    for (const QString &clientId : priorityOrder) {
        if (tracker.isAvailable(clientId)) {
            return clientId;
        }
    }
    return QString(); // 全枯渇
}
