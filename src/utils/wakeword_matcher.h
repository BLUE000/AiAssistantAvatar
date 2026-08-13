#pragma once
#include <QString>
#include <QStringList>

class WakewordMatcher {
public:
    static bool matchAndStrip(const QString &inputText,
                              const QString &targetKeyword,
                              const QStringList &aliases,
                              QString &outStrippedText);

    static QString toKatakana(const QString &src);
    static QString normalizeForMatch(const QString &src);
    static QString stripKeywordWithHonorifics(const QString &src, const QString &keyword);
    static int calculateLevenshteinDistance(const QString &s1, const QString &s2);
    static double calculateSimilarity(const QString &s1, const QString &s2);
};
