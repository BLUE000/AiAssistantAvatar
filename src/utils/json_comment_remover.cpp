#include "json_comment_remover.h"
#include <QStringList>

QByteArray JsonCommentRemover::stripHashComments(const QByteArray &jsonBytes) {
    QString text = QString::fromUtf8(jsonBytes);
    QString resultText = stripHashComments(text);
    return resultText.toUtf8();
}

QString JsonCommentRemover::stripHashComments(const QString &jsonText) {
    QString normalizedText = jsonText;
    normalizedText.replace("\r\n", "\n").replace("\r", "\n");
    QStringList lines = normalizedText.split('\n');
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

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

static QString formatJsonValue(const QJsonValue &val) {
    if (val.isString()) {
        QString s = val.toString();
        s.replace("\\", "\\\\");
        s.replace("\"", "\\\"");
        s.replace("\n", "\\n");
        s.replace("\r", "\\r");
        s.replace("\t", "\\t");
        return QString("\"%1\"").arg(s);
    } else if (val.isBool()) {
        return val.toBool() ? "true" : "false";
    } else if (val.isDouble()) {
        double d = val.toDouble();
        if (d == static_cast<int64_t>(d)) {
            return QString::number(static_cast<int64_t>(d));
        }
        return QString::number(d);
    } else if (val.isNull()) {
        return "null";
    } else {
        QJsonDocument doc;
        if (val.isObject()) doc.setObject(val.toObject());
        else if (val.isArray()) doc.setArray(val.toArray());
        return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    }
}

QString JsonCommentRemover::updateExistingJsonText(const QString &existingText, const QJsonObject &newObj) {
    if (existingText.trimmed().isEmpty()) {
        QJsonDocument doc(newObj);
        return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    }

    QString normalizedText = existingText;
    normalizedText.replace("\r\n", "\n").replace("\r", "\n");
    QStringList lines = normalizedText.split('\n');
    QSet<QString> processedKeys;
    QStringList resultLines;

    for (int lineIdx = 0; lineIdx < lines.size(); ++lineIdx) {
        QString line = lines.at(lineIdx);
        QString trimmed = line.trimmed();

        // コメント行や空行は無条件にそのまま保存
        if (trimmed.startsWith('#') || trimmed.isEmpty()) {
            resultLines.append(line);
            continue;
        }

        // キーの検索（例: "key": または "key" :）
        QRegularExpression keyRegex("^(\\s*)\"([^\"]+)\"\\s*:\\s*(.*)$");
        QRegularExpressionMatch match = keyRegex.match(line);

        if (match.hasMatch()) {
            QString indent = match.captured(1);
            QString key = match.captured(2);
            QString rest = match.captured(3);

            if (newObj.contains(key)) {
                processedKeys.insert(key);
                QJsonValue newVal = newObj.value(key);

                // オブジェクトや配列の場合は QJsonDocument コマンドで整形
                if (newVal.isObject() || newVal.isArray()) {
                    resultLines.append(line);
                } else {
                    // 行末コメント (# ...) や末尾カンマ (,) の保持
                    QString commentPart;
                    bool hasComma = false;
                    
                    // コメント位置の検索
                    bool inStr = false;
                    int commentIdx = -1;
                    for (int i = 0; i < rest.size(); ++i) {
                        if (rest.at(i) == '"' && (i == 0 || rest.at(i-1) != '\\')) inStr = !inStr;
                        if (!inStr && rest.at(i) == '#') {
                            commentIdx = i;
                            break;
                        }
                    }

                    QString valueAndComma = rest;
                    if (commentIdx >= 0) {
                        commentPart = rest.mid(commentIdx);
                        valueAndComma = rest.left(commentIdx);
                    }

                    if (valueAndComma.trimmed().endsWith(',')) {
                        hasComma = true;
                    }

                    QString valStr = formatJsonValue(newVal);
                    if (hasComma) valStr += ",";
                    if (!commentPart.isEmpty()) valStr += " " + commentPart;

                    resultLines.append(QString("%1\"%2\": %3").arg(indent, key, valStr));
                }
            } else {
                resultLines.append(line);
            }
        } else {
            resultLines.append(line);
        }
    }

    // 既存ファイル内に存在しなかった新規キーを末尾（最終閉じブレース } の直前）に追加
    QStringList newKeys;
    for (auto it = newObj.constBegin(); it != newObj.constEnd(); ++it) {
        if (!processedKeys.contains(it.key())) {
            newKeys.append(it.key());
        }
    }

    if (!newKeys.isEmpty()) {
        int lastBraceIdx = -1;
        for (int i = resultLines.size() - 1; i >= 0; --i) {
            if (resultLines.at(i).trimmed() == "}" || resultLines.at(i).trimmed() == "};") {
                lastBraceIdx = i;
                break;
            }
        }

        QStringList addedLines;
        for (int i = 0; i < newKeys.size(); ++i) {
            const QString &k = newKeys.at(i);
            QString valStr = formatJsonValue(newObj.value(k));
            QString comma = (i == newKeys.size() - 1) ? "" : ",";
            addedLines.append(QString("  \"%1\": %2%3").arg(k, valStr, comma));
        }

        if (lastBraceIdx >= 0) {
            // 前の行にカンマがなければ補完
            if (lastBraceIdx > 0 && !resultLines.at(lastBraceIdx - 1).trimmed().endsWith(',') && !resultLines.at(lastBraceIdx - 1).trimmed().endsWith('{')) {
                resultLines[lastBraceIdx - 1] = resultLines.at(lastBraceIdx - 1).trimmed() + ",";
            }
            for (int i = 0; i < addedLines.size(); ++i) {
                resultLines.insert(lastBraceIdx + i, addedLines.at(i));
            }
        } else {
            resultLines.append(addedLines);
        }
    }

    return resultLines.join('\n');
}

