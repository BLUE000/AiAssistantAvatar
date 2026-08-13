#include "wakeword_matcher.h"
#include <QChar>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>

QString WakewordMatcher::toKatakana(const QString &src) {
    QString res = src;
    for (int i = 0; i < res.length(); ++i) {
        ushort ch = res[i].unicode();
        if (ch >= 0x3041 && ch <= 0x3096) {
            res[i] = QChar(ch + 0x0060);
        }
    }
    return res;
}

QString WakewordMatcher::normalizeForMatch(const QString &src) {
    QString s = toKatakana(src.toLower());
    // 四つ仮名統一
    s.replace(QChar(0x30C2), QChar(0x30B8)); // ヂ -> ジ
    s.replace(QChar(0x30C5), QChar(0x30BA)); // ヅ -> ズ

    // 濁音・半濁音の表記ゆれ（例: プル -> ブル）
    s.replace(QChar(0x30D7), QChar(0x30D6)); // プ -> ブ
    s.replace(QChar(0x30D8), QChar(0x30D5)); // ペ -> ベ
    s.replace(QChar(0x30D1), QChar(0x30D0)); // パ -> バ
    s.replace(QChar(0x30D4), QChar(0x30D3)); // ピ -> ビ
    s.replace(QChar(0x30D9), QChar(0x30D6)); // ポ -> ボ

    // 名前の定番漢字・長音表記ゆれ（例: 太郎 / タロー / たろー -> タロウ）
    s.replace("太郎", "タロウ");
    s.replace("タロー", "タロウ");
    s.replace("たろー", "タロウ");
    s.replace("ロー", "ロウ");

    // 空白・記号の除去
    s.remove(QChar(0x3000));
    s.remove(' ');
    s.remove(QRegularExpression("[、。！？!?,.\\-_~〜ー]"));
    return s;
}

QString WakewordMatcher::stripKeywordWithHonorifics(const QString &src, const QString &keyword) {
    if (keyword.isEmpty()) return src;

    QString katakanaKw = toKatakana(keyword);
    QString hiraganaKw = keyword;
    for (int i = 0; i < hiraganaKw.length(); ++i) {
        ushort ch = hiraganaKw[i].unicode();
        if (ch >= 0x30A1 && ch <= 0x30F6) hiraganaKw[i] = QChar(ch - 0x0060);
    }

    QStringList prefixList;
    prefixList << "ぶる" << "ブル" << "プル" << "ぶ" << "ブ" << "プ";

    QStringList suffixList;
    suffixList << "たろう" << "タロウ" << "太郎" << "タロー" << "たろー";

    QSet<QString> variantSet;
    variantSet.insert(keyword);
    variantSet.insert(katakanaKw);
    variantSet.insert(hiraganaKw);

    bool containsPrefix = false;
    for (const QString &p : prefixList) {
        if (keyword.contains(p)) { containsPrefix = true; break; }
    }
    bool containsSuffix = false;
    for (const QString &s : suffixList) {
        if (keyword.contains(s)) { containsSuffix = true; break; }
    }

    if (containsPrefix && containsSuffix) {
        for (const QString &p : prefixList) {
            for (const QString &s : suffixList) {
                variantSet.insert(p + s);
            }
        }
    }

    QStringList variants = variantSet.values();
    std::sort(variants.begin(), variants.end(), [](const QString &a, const QString &b) {
        return a.length() > b.length();
    });

    QStringList escapedVariants;
    for (const QString &v : variants) {
        escapedVariants << QRegularExpression::escape(v);
    }

    QString patternStr = "(?:" + escapedVariants.join("|") + ")(?:くん|君|さん|ちゃん|様|たん|殿|氏|ー|〜)*[、。！？!?\\s\\t,.]*";
    QRegularExpression regex(patternStr, QRegularExpression::CaseInsensitiveOption);
    QString result = src;
    result.replace(regex, "");
    return result.trimmed();
}

int WakewordMatcher::calculateLevenshteinDistance(const QString &s1, const QString &s2) {
    const int m = s1.length();
    const int n = s2.length();
    if (m == 0) return n;
    if (n == 0) return m;

    std::vector<std::vector<int>> d(m + 1, std::vector<int>(n + 1));
    for (int i = 0; i <= m; ++i) d[i][0] = i;
    for (int j = 0; j <= n; ++j) d[0][j] = j;

    for (int i = 1; i <= m; ++i) {
        for (int j = 1; j <= n; ++j) {
            int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
            d[i][j] = std::min({ d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost });
        }
    }
    return d[m][n];
}

double WakewordMatcher::calculateSimilarity(const QString &s1, const QString &s2) {
    int maxLen = std::max(s1.length(), s2.length());
    if (maxLen == 0) return 1.0;
    int dist = calculateLevenshteinDistance(s1, s2);
    return 1.0 - (double)dist / maxLen;
}

bool WakewordMatcher::matchAndStrip(const QString &inputText,
                                   const QString &targetKeyword,
                                   const QStringList &aliases,
                                   QString &outStrippedText) {
    outStrippedText = inputText.trimmed();
    if (targetKeyword.isEmpty()) return true;

    QString normInput = normalizeForMatch(inputText);
    QString normTarget = normalizeForMatch(targetKeyword);

    bool isMatched = false;
    QString matchedKeyword = targetKeyword;

    if (normInput.contains(normTarget)) {
        isMatched = true;
    } else {
        for (const QString &alias : aliases) {
            if (alias.isEmpty()) continue;
            QString normAlias = normalizeForMatch(alias);
            if (normInput.contains(normAlias)) {
                isMatched = true;
                matchedKeyword = alias;
                break;
            }
        }
    }

    if (!isMatched) {
        // 音素編集距離 (Levenshtein Distance) 曖昧照合 (類似度 75% 以上)
        double sim = calculateSimilarity(normInput, normTarget);
        if (sim >= 0.75) {
            isMatched = true;
        }
    }

    if (isMatched) {
        outStrippedText = stripKeywordWithHonorifics(inputText, matchedKeyword);
        return true;
    }

    return false;
}
