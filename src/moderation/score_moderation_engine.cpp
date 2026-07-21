#include "score_moderation_engine.h"
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>

ScoreModerationEngine::ScoreModerationEngine() {
}

ScoreModerationEngine& ScoreModerationEngine::instance() {
    static ScoreModerationEngine engine;
    return engine;
}

bool ScoreModerationEngine::loadBlacklist(const QString &filePath) {
    QString actualPath = filePath;
    if (!QFile::exists(actualPath)) {
        actualPath = QCoreApplication::applicationDirPath() + "/" + filePath;
    }
    if (!QFile::exists(actualPath)) {
        actualPath = QCoreApplication::applicationDirPath() + "/../" + filePath;
    }
    if (!QFile::exists(actualPath)) {
        actualPath = QCoreApplication::applicationDirPath() + "/../../" + filePath;
    }
#ifdef PROJECT_SOURCE_DIR
    if (!QFile::exists(actualPath)) {
        actualPath = QString(PROJECT_SOURCE_DIR) + "/" + filePath;
    }
#endif

    QFile file(actualPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Failed to open blacklist file:" << filePath << "Tried:" << actualPath;
        return false;
    }

    m_blacklistRules.clear();
    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith("#")) continue;

        QStringList parts = line.split(",");
        if (parts.size() >= 3) {
            ModerationRule rule;
            rule.pattern = parts[0].trimmed();
            rule.category = parts[1].trimmed();
            rule.score = parts[2].trimmed().toInt();
            m_blacklistRules.append(rule);
        }
    }
    file.close();
    return true;
}

bool ScoreModerationEngine::loadWhitelist(const QString &filePath) {
    QString actualPath = filePath;
    if (!QFile::exists(actualPath)) {
        actualPath = QCoreApplication::applicationDirPath() + "/" + filePath;
    }
    if (!QFile::exists(actualPath)) {
        actualPath = QCoreApplication::applicationDirPath() + "/../" + filePath;
    }
    if (!QFile::exists(actualPath)) {
        actualPath = QCoreApplication::applicationDirPath() + "/../../" + filePath;
    }
#ifdef PROJECT_SOURCE_DIR
    if (!QFile::exists(actualPath)) {
        actualPath = QString(PROJECT_SOURCE_DIR) + "/" + filePath;
    }
#endif

    QFile file(actualPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Failed to open whitelist file:" << filePath << "Tried:" << actualPath;
        return false;
    }

    m_whitelistRules.clear();
    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith("#")) continue;

        QStringList parts = line.split(",");
        if (parts.size() >= 3) {
            ModerationRule rule;
            rule.pattern = parts[0].trimmed();
            rule.category = parts[1].trimmed();
            rule.score = parts[2].trimmed().toInt();
            m_whitelistRules.append(rule);
        }
    }
    file.close();
    return true;
}

ModerationEvalResult ScoreModerationEngine::evaluate(const QString &inputText, const QStringList &recentHistory) {
    ModerationEvalResult result;
    result.maskedText = inputText;

    // 1. ブラックリスト評価（カテゴリスコア ＆ 意図判定）
    for (const auto &rule : m_blacklistRules) {
        if (rule.pattern.isEmpty()) continue;

        QRegularExpression re(rule.pattern, QRegularExpression::CaseInsensitiveOption);
        if (re.isValid() && inputText.contains(re)) {
            result.categoryScore += rule.score;
            if (!result.matchedCategories.contains(rule.category)) {
                result.matchedCategories.append(rule.category);
            }

            if (rule.category == "instruction") {
                result.hasInstruction = true;
                result.instructionScore += rule.score;
            } else if (rule.category == "personal_info") {
                result.hasPersonalInfo = true;
            } else if (rule.category == "politics" || rule.category == "religion") {
                result.hasPoliticsOrReligion = true;
            }

            // WARN以上の場合はマスク置換の準備
            if (rule.score >= 30) {
                QString stars(qMax(2, (int)rule.pattern.length()), '*');
                result.maskedText.replace(re, stars);
            }
        }
    }

    // 2. ホワイトリスト評価（単語・ゲーム・感情文脈による減算）
    for (const auto &rule : m_whitelistRules) {
        if (rule.pattern.isEmpty()) continue;

        QRegularExpression re(rule.pattern, QRegularExpression::CaseInsensitiveOption);
        if (re.isValid() && inputText.contains(re)) {
            result.contextDeduction += rule.score;
        }
    }

    // 3. 直近会話履歴（history_context）の文脈評価
    int historyDeduction = 0;
    for (const QString &histMsg : recentHistory) {
        for (const auto &rule : m_whitelistRules) {
            if (rule.pattern.isEmpty()) continue;
            QRegularExpression re(rule.pattern, QRegularExpression::CaseInsensitiveOption);
            if (re.isValid() && histMsg.contains(re)) {
                historyDeduction += 30; // 履歴からの文脈持続減点
                break;
            }
        }
    }

    // Jailbreak Guard: 危険意図 (instruction / personal_info) がある場合は過去履歴の減算を強制キャンセル (0固定)
    if (result.hasInstruction || result.hasPersonalInfo) {
        historyDeduction = 0;
    }

    result.contextDeduction += historyDeduction;

    // 4. 最終危険度スコア算出: (カテゴリスコア) - (文脈補正)
    result.totalScore = qMax(0, result.categoryScore - result.contextDeduction);

    // 5. 判定アクションの決定
    if (result.hasPersonalInfo || result.totalScore >= 70) {
        result.action = ModerationAction::BLOCK;
    } else if (result.totalScore >= 30) {
        result.action = ModerationAction::WARN;
    } else {
        result.action = ModerationAction::SAFE;
    }

    return result;
}
