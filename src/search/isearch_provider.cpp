#include "isearch_provider.h"

ISearchProvider::ISearchProvider(QObject *parent)
    : QObject(parent)
{
}

ISearchProvider::~ISearchProvider() {
}

#include <QRegularExpression>
#include <QStringList>

QString ISearchProvider::cleanseSnippet(const QString &text, int maxChars) {
    if (text.isEmpty()) return QString();

    QStringList lines = text.split(QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts);
    QStringList filteredLines;

    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty()) continue;

        // 1. エラー定型文の除去
        if (line.contains("ページを表示することが出来ませんでした") ||
            line.contains("ブラウザの「戻る」ボタン") ||
            line.contains("404 Not Found") ||
            line.contains("Access Denied")) {
            continue;
        }

        // 2. パンくずナビゲーションの除去 (例: ホーム > 各種データ・資料 > ...)
        if (line.count('>') >= 2 && line.contains("ホーム")) {
            continue;
        }

        // 3. 連続する年号・月日の羅列テーブル行の除去 (例: | 2026年 2025年 2024年... や | 1月 2月 3月...)
        int yearCount = line.count(QRegularExpression("\\b\\d{4}年"));
        int monthCount = line.count(QRegularExpression("\\b\\d{1,2}月"));
        int dayCount = line.count(QRegularExpression("\\b\\d{1,2}日"));
        if (yearCount >= 4 || monthCount >= 6 || dayCount >= 8) {
            continue;
        }

        // 4. テーブルの区切り線のみの行 (例: | --- | --- |) の除去
        if (line.startsWith('|') && line.contains("---") && !line.contains(QRegularExpression("[a-zA-Z0-9\\p{Han}\\p{Hiragana}\\p{Katakana}]"))) {
            continue;
        }

        // 連続空白を単一スペースに正規化
        line.replace(QRegularExpression("[\\t ]+"), " ");
        filteredLines.append(line);
    }

    QString joined = filteredLines.join(" ").trimmed();
    joined.replace(QRegularExpression("[\\t ]+"), " ");

    if (maxChars > 0 && joined.length() > maxChars) {
        joined = joined.left(maxChars).trimmed() + "...";
    }

    return joined;
}
