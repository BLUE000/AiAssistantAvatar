#ifndef SCORE_MODERATION_ENGINE_H
#define SCORE_MODERATION_ENGINE_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <QVector>

struct ModerationRule {
    QString pattern;
    QString category;
    int score;
};

enum class ModerationAction {
    SAFE,   // 通過 (0 - 29点)
    WARN,   // 伏字 / マイルド化 (30 - 69点)
    BLOCK   // 要求拒否 (70点以上)
};

struct ModerationEvalResult {
    int totalScore = 0;
    int categoryScore = 0;
    int instructionScore = 0;
    int contextDeduction = 0;
    ModerationAction action = ModerationAction::SAFE;
    bool hasInstruction = false;
    bool hasPoliticsOrReligion = false;
    bool hasPersonalInfo = false;
    QString maskedText;
    QStringList matchedCategories;
};

class ScoreModerationEngine {
public:
    ScoreModerationEngine();

    bool loadBlacklist(const QString &filePath);
    bool loadWhitelist(const QString &filePath);

    ModerationEvalResult evaluate(const QString &inputText, const QStringList &recentHistory = QStringList());

    static ScoreModerationEngine& instance();

private:
    QVector<ModerationRule> m_blacklistRules;
    QVector<ModerationRule> m_whitelistRules;
};

#endif // SCORE_MODERATION_ENGINE_H
