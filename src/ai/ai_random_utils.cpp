#include "ai_random_utils.h"
#include <QRandomGenerator>
#include <QRegularExpression>
#include <algorithm>

namespace AIRandomUtils {

int getRandom(int min, int max) {
    if (min > max) {
        std::swap(min, max);
    }
    if (min == max) {
        return min;
    }
    return QRandomGenerator::global()->bounded(min, max + 1);
}

int getDailyRandom(int min, int max, const QString &seed) {
    if (min > max) {
        std::swap(min, max);
    }
    if (min == max) {
        return min;
    }
    quint32 hashVal = qHash(seed);
    quint32 range = static_cast<quint32>(max - min + 1);
    return min + static_cast<int>(hashVal % range);
}

QList<int> getRandomList(int max, int count) {
    if (max < 0 || count <= 0) {
        return QList<int>();
    }

    int totalCandidates = max + 1;
    if (count > totalCandidates) {
        count = totalCandidates;
    }

    // 0 ~ max までの候補リストを準備
    QList<int> candidates;
    candidates.reserve(totalCandidates);
    for (int i = 0; i <= max; ++i) {
        candidates.append(i);
    }

    // Fisher-Yates シャッフルでランダム抽出
    for (int i = candidates.size() - 1; i > 0; --i) {
        int j = QRandomGenerator::global()->bounded(i + 1);
        candidates.swapItemsAt(i, j);
    }

    return candidates.mid(0, count);
}

QString parseAndEvaluate(const QString &text) {
    if (text.isEmpty()) return text;

    QString result = text;

    // 1. RandomList(max, count) のパース・評価
    QRegularExpression listRegex("RandomList\\(\\s*(-?\\d+)\\s*,\\s*(-?\\d+)\\s*\\)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator listIt = listRegex.globalMatch(result);
    while (listIt.hasNext()) {
        QRegularExpressionMatch match = listIt.next();
        QString fullMatch = match.captured(0);
        int max = match.captured(1).toInt();
        int count = match.captured(2).toInt();

        QList<int> listRet = getRandomList(max, count);
        QStringList strItems;
        for (int val : listRet) {
            strItems.append(QString::number(val));
        }
        QString replacement = strItems.join(", ");
        result.replace(fullMatch, replacement);
    }

    // 2. DailyRandom(min, max, "seed") または DailyRandom(min, max) のパース・評価
    QRegularExpression dailyRandRegex("DailyRandom\\(\\s*(-?\\d+)\\s*,\\s*(-?\\d+)(?:\\s*,\\s*\"?([^\"]*)\"?)?\\s*\\)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator dailyRandIt = dailyRandRegex.globalMatch(result);
    while (dailyRandIt.hasNext()) {
        QRegularExpressionMatch match = dailyRandIt.next();
        QString fullMatch = match.captured(0);
        int min = match.captured(1).toInt();
        int max = match.captured(2).toInt();
        QString seed = match.captured(3);

        int val = getDailyRandom(min, max, seed);
        result.replace(fullMatch, QString::number(val));
    }

    // 3. Random(min, max) のパース・評価
    QRegularExpression randRegex("Random\\(\\s*(-?\\d+)\\s*,\\s*(-?\\d+)\\s*\\)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator randIt = randRegex.globalMatch(result);
    while (randIt.hasNext()) {
        QRegularExpressionMatch match = randIt.next();
        QString fullMatch = match.captured(0);
        int min = match.captured(1).toInt();
        int max = match.captured(2).toInt();

        int val = getRandom(min, max);
        result.replace(fullMatch, QString::number(val));
    }

    return result;
}


} // namespace AIRandomUtils
