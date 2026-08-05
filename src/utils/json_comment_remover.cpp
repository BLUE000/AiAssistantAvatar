#include "json_comment_remover.h"
#include <QStringList>

QByteArray JsonCommentRemover::stripHashComments(const QByteArray &jsonBytes) {
    QString text = QString::fromUtf8(jsonBytes);
    QString resultText = stripHashComments(text);
    return resultText.toUtf8();
}

QString JsonCommentRemover::stripHashComments(const QString &jsonText) {
    QStringList lines = jsonText.split('\n');
    QStringList resultLines;

    for (const QString &line : lines) {
        QString cleanLine;
        bool inString = false;
        bool escaped = false;

        for (int i = 0; i < line.size(); ++i) {
            QChar ch = line.at(i);

            if (escaped) {
                cleanLine.append(ch);
                escaped = false;
                continue;
            }

            if (ch == '\\') {
                cleanLine.append(ch);
                escaped = true;
                continue;
            }

            if (ch == '"') {
                inString = !inString;
                cleanLine.append(ch);
                continue;
            }

            if (!inString && ch == '#') {
                // 行頭または項目末尾の # コメントをここでカット（行末まで破棄）
                break;
            }

            cleanLine.append(ch);
        }

        resultLines.append(cleanLine);
    }

    return resultLines.join('\n');
}
