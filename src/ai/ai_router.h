#pragma once
#include <QString>
#include <QStringList>

class RateLimitTracker;

enum class AIRole { Manager, Worker };

/// 優先度順にAvailableなクライアントを選択するルーター（純粋C++ロジック）
class AIRouter {
public:
    /// priorityOrder の順に isAvailable == true の最初のクライアントIDを返す。
    /// 全クライアントが unavailable なら空文字を返す。
    QString selectClient(AIRole role,
                         const RateLimitTracker &tracker,
                         const QStringList &priorityOrder) const;
};
