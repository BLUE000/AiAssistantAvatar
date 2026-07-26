#include "markdown_table_engine.h"
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QCoreApplication>

MarkdownTableEngine::MarkdownTableEngine(const QString &rootDir)
    : m_rootDir(rootDir)
{
    reload();
}

MarkdownTableEngine::~MarkdownTableEngine() {
}

bool MarkdownTableEngine::isPathSafe(const QString &path) const {
    if (path.contains("..") || path.contains(":\\") || path.startsWith("/") || path.startsWith("\\")) {
        // 相対パス抜け出しまたはルートルート指定は不正とみなす
        QFileInfo targetInfo(path);
        QFileInfo rootInfo(m_rootDir);
        
        QString canonicalTarget = targetInfo.canonicalFilePath();
        QString canonicalRoot = rootInfo.canonicalFilePath();
        
        if (!canonicalTarget.isEmpty() && !canonicalRoot.isEmpty()) {
            return canonicalTarget.startsWith(canonicalRoot);
        }
        return false;
    }
    return true;
}

void MarkdownTableEngine::reload() {
    m_tables.clear();
    m_indexEntries.clear();
    m_diagnostics.clear();

    QString actualRoot = m_rootDir;
    if (!QDir(actualRoot).exists()) {
        QDir cur;
        if (cur.exists("knowledge")) {
            actualRoot = "knowledge";
        }
    }

    if (!QDir(actualRoot).exists()) {
        qDebug() << "MarkdownTableEngine: Knowledge root directory does not exist:" << actualRoot;
        return;
    }

    scanDirectory(actualRoot, "", "");
    QJsonObject indexObj;
    buildIndexAndValidate(indexObj, m_diagnostics);
    qDebug() << "MarkdownTableEngine: Loaded" << m_tables.size() << "valid tables from knowledge repository. Diagnostics count:" << m_diagnostics.size();
}

bool MarkdownTableEngine::buildIndexAndValidate(QJsonObject &outIndexData, QList<KnowledgeIndexEntry> &outDiagnostics) {
    outDiagnostics.clear();
    QJsonObject triggersObj;
    QJsonArray diagnosticsArr;

    for (const KnowledgeIndexEntry &entry : m_indexEntries) {
        if (!entry.isValid) {
            QJsonObject diagObj;
            diagObj["file_path"] = entry.filePath;
            diagObj["line_number"] = entry.errorLine;
            diagObj["error_type"] = "syntax_error";
            diagObj["message"] = entry.errorMessage;
            diagnosticsArr.append(diagObj);
            outDiagnostics.append(entry);
            continue;
        }

        QJsonObject entryObj;
        entryObj["file_path"] = entry.filePath;
        entryObj["group"] = entry.group;
        entryObj["category"] = entry.category;
        entryObj["title"] = entry.title;
        entryObj["priority"] = entry.priority;
        entryObj["mode"] = entry.mode;
        entryObj["status"] = "valid";

        QJsonArray headersArr;
        for (const QString &h : entry.headers) {
            headersArr.append(h);
        }
        entryObj["columns"] = headersArr;

        for (const QString &trig : entry.triggers) {
            QJsonArray arr = triggersObj.value(trig).toArray();
            arr.append(entryObj);
            triggersObj[trig] = arr;
        }
    }

    outIndexData["version"] = "1.0";
    outIndexData["triggers"] = triggersObj;
    outIndexData["diagnostics"] = diagnosticsArr;

    // knowledge_index.json の保存
    QFile indexFile("knowledge_index.json");
    if (indexFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QJsonDocument doc(outIndexData);
        indexFile.write(doc.toJson(QJsonDocument::Indented));
        indexFile.close();
    }
    return true;
}

KnowledgeIndexEntry MarkdownTableEngine::resolveBestEntryForTrigger(const QString &triggerWord) const {
    KnowledgeIndexEntry bestEntry;
    int highestPriority = -1;

    for (const KnowledgeIndexEntry &entry : m_indexEntries) {
        if (!entry.isValid) continue;
        for (const QString &trig : entry.triggers) {
            if (trig.compare(triggerWord, Qt::CaseInsensitive) == 0 ||
                triggerWord.contains(trig, Qt::CaseInsensitive)) {
                if (entry.priority > highestPriority) {
                    highestPriority = entry.priority;
                    bestEntry = entry;
                }
            }
        }
    }

    return bestEntry;
}

void MarkdownTableEngine::scanDirectory(const QString &dirPath, const QString &currentGroup, const QString &currentCategory) {
    QDir dir(dirPath);
    QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);

    for (const QFileInfo &entry : entries) {
        if (entry.isDir()) {
            QString nextGroup = currentGroup;
            QString nextCategory = currentCategory;

            if (currentGroup.isEmpty()) {
                nextGroup = entry.fileName();
            } else if (currentCategory.isEmpty()) {
                nextCategory = entry.fileName();
            } else {
                nextCategory += "/" + entry.fileName();
            }
            scanDirectory(entry.absoluteFilePath(), nextGroup, nextCategory);
        } else if (entry.isFile() && (entry.suffix().toLower() == "md" || entry.suffix().toLower() == "txt")) {
            QString grp = currentGroup.isEmpty() ? "Default" : currentGroup;
            QString cat = currentCategory.isEmpty() ? "General" : currentCategory;
            parseMarkdownFile(entry.absoluteFilePath(), grp, cat);
        }
    }
}

void MarkdownTableEngine::parseMarkdownFile(const QString &filePath, const QString &group, const QString &category) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QTextStream in(&file);
    QStringList lines;
    while (!in.atEnd()) {
        lines.append(in.readLine().trimmed());
    }
    file.close();

    QFileInfo fileInfo(filePath);
    QString tableName = fileInfo.completeBaseName();

    TableRecord record;
    record.group = group;
    record.category = category;
    record.tableName = tableName;
    record.filePath = filePath;

    KnowledgeIndexEntry indexEntry;
    indexEntry.filePath = filePath;
    indexEntry.group = group;
    indexEntry.category = category;
    indexEntry.tableName = tableName;
    indexEntry.title = tableName;
    indexEntry.isValid = true;

    bool headersParsed = false;
    QStringList headers;
    int expectedColumnCount = -1;
    bool inTriggerSection = false;
    bool inPrioritySection = false;
    bool inModeSection = false;

    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines.at(i);
        if (line.isEmpty()) continue;

        // セクション判定 (# トリガー, # 優先度, # 処理モード)
        if (line.startsWith("#")) {
            QString headerText = line.section('#', 1).trimmed();
            inTriggerSection = (headerText.contains("トリガー", Qt::CaseInsensitive) || headerText.contains("Trigger", Qt::CaseInsensitive));
            inPrioritySection = (headerText.contains("優先度", Qt::CaseInsensitive) || headerText.contains("Priority", Qt::CaseInsensitive));
            inModeSection = (headerText.contains("処理", Qt::CaseInsensitive) || headerText.contains("Mode", Qt::CaseInsensitive));
            continue;
        }

        if (inTriggerSection && (line.startsWith("-") || line.startsWith("*"))) {
            QString trig = line.mid(1).trimmed();
            if (!trig.isEmpty()) indexEntry.triggers.append(trig);
            continue;
        }

        if (inPrioritySection && (line.startsWith("-") || line.startsWith("*"))) {
            QString prioStr = line.mid(1).trimmed();
            bool ok = false;
            int prio = prioStr.toInt(&ok);
            if (ok) indexEntry.priority = prio;
            continue;
        }

        if (inModeSection && (line.startsWith("-") || line.startsWith("*"))) {
            indexEntry.mode = line.mid(1).trimmed();
            continue;
        }

        // テーブル構造パース ＆ バリデーション
        if (line.contains("|")) {
            QString trimmedLine = line;
            if (!trimmedLine.startsWith("|")) trimmedLine = "|" + trimmedLine;
            if (!trimmedLine.endsWith("|")) trimmedLine = trimmedLine + "|";

            QStringList cells;
            QStringList rawCells = trimmedLine.split("|");
            for (int c = 1; c < rawCells.size() - 1; ++c) {
                cells.append(rawCells.at(c).trimmed());
            }

            bool isSeparator = true;
            for (const QString &cell : cells) {
                QString cleaned = cell;
                cleaned.remove(':').remove('-');
                if (!cleaned.isEmpty()) {
                    isSeparator = false;
                    break;
                }
            }
            if (isSeparator) continue;

            if (!headersParsed) {
                headers = cells;
                expectedColumnCount = cells.size();
                record.headers = headers;
                indexEntry.headers = headers;
                headersParsed = true;
            } else {
                if (cells.size() != expectedColumnCount) {
                    indexEntry.isValid = false;
                    indexEntry.errorMessage = QString("テーブルの列数が一致しません (期待値: %1 列, 検出値: %2 列)").arg(expectedColumnCount).arg(cells.size());
                    indexEntry.errorLine = i + 1;
                    m_indexEntries.append(indexEntry);
                    qDebug() << "MarkdownTableEngine: Validation error in file:" << filePath << "Line:" << indexEntry.errorLine << indexEntry.errorMessage;
                    return;
                }

                QMap<QString, QString> rowMap;
                for (int c = 0; c < cells.size() && c < headers.size(); ++c) {
                    rowMap[headers.at(c)] = cells.at(c);
                }
                record.rows.append(rowMap);
            }
        }
    }

    if (!headers.isEmpty() && record.rows.size() > 0) {
        m_tables.append(record);
        m_indexEntries.append(indexEntry);
    }
}

QString MarkdownTableEngine::queryColumn(const QString &group, const QString &category, const QString &table, const QString &searchKey, const QString &targetColumn) const {
    for (const TableRecord &rec : m_tables) {
        if (group.isEmpty() || rec.group.contains(group, Qt::CaseInsensitive)) {
            if (category.isEmpty() || rec.category.contains(category, Qt::CaseInsensitive)) {
                if (table.isEmpty() || rec.tableName.contains(table, Qt::CaseInsensitive)) {
                    for (const QMap<QString, QString> &row : rec.rows) {
                        // いずれかのカラムの値が searchKey に一致するかチェック
                        bool keyMatched = false;
                        for (auto it = row.constBegin(); it != row.constEnd(); ++it) {
                            if (it.value().contains(searchKey, Qt::CaseInsensitive)) {
                                keyMatched = true;
                                break;
                            }
                        }
                        if (keyMatched) {
                            if (row.contains(targetColumn)) {
                                return row.value(targetColumn);
                            }
                            // もし指定カラム名が部分一致する場合はフォールバック
                            for (auto it = row.constBegin(); it != row.constEnd(); ++it) {
                                if (it.key().contains(targetColumn, Qt::CaseInsensitive)) {
                                    return it.value();
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return "";
}

QString MarkdownTableEngine::selectRandomColumn(const QString &group, const QString &category, const QString &table, const QString &targetColumn) const {
    QList<QString> candidates;
    for (const TableRecord &rec : m_tables) {
        if (group.isEmpty() || rec.group.contains(group, Qt::CaseInsensitive)) {
            if (category.isEmpty() || rec.category.contains(category, Qt::CaseInsensitive)) {
                if (table.isEmpty() || rec.tableName.contains(table, Qt::CaseInsensitive)) {
                    for (const QMap<QString, QString> &row : rec.rows) {
                        if (targetColumn.isEmpty()) {
                            // 全カラムを結合して返却対象にする
                            QStringList vals;
                            for (auto it = row.constBegin(); it != row.constEnd(); ++it) {
                                vals.append(QString("%1: %2").arg(it.key(), it.value()));
                            }
                            candidates.append(vals.join(", "));
                        } else if (row.contains(targetColumn)) {
                            candidates.append(row.value(targetColumn));
                        } else {
                            for (auto it = row.constBegin(); it != row.constEnd(); ++it) {
                                if (it.key().contains(targetColumn, Qt::CaseInsensitive)) {
                                    candidates.append(it.value());
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (candidates.isEmpty()) return "";
    int idx = QRandomGenerator::global()->bounded(candidates.size());
    return candidates.at(idx);
}

QString MarkdownTableEngine::parseAndEvaluate(const QString &text) const {
    if (text.isEmpty()) return text;
    QString result = text;

    // 1. TableSearch("グループ", "カテゴリ", "テーブル", "検索キー", "対象カラム") の置換
    QRegularExpression searchRegex("TableSearch\\(\\s*\"([^\"]*)\"\\s*,\\s*\"([^\"]*)\"\\s*,\\s*\"([^\"]*)\"\\s*,\\s*\"([^\"]*)\"\\s*,\\s*\"([^\"]*)\"\\s*\\)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator sIt = searchRegex.globalMatch(result);
    while (sIt.hasNext()) {
        QRegularExpressionMatch m = sIt.next();
        QString fullMatch = m.captured(0);
        QString grp = m.captured(1);
        QString cat = m.captured(2);
        QString tbl = m.captured(3);
        QString key = m.captured(4);
        QString col = m.captured(5);

        QString val = queryColumn(grp, cat, tbl, key, col);
        result.replace(fullMatch, val);
    }

    // 2. TableSelectRandom("グループ", "カテゴリ", "テーブル", "対象カラム") の置換
    QRegularExpression randRegex("TableSelectRandom\\(\\s*\"([^\"]*)\"\\s*,\\s*\"([^\"]*)\"\\s*,\\s*\"([^\"]*)\"\\s*,\\s*\"([^\"]*)\"\\s*\\)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator rIt = randRegex.globalMatch(result);
    while (rIt.hasNext()) {
        QRegularExpressionMatch m = rIt.next();
        QString fullMatch = m.captured(0);
        QString grp = m.captured(1);
        QString cat = m.captured(2);
        QString tbl = m.captured(3);
        QString col = m.captured(4);

        QString val = selectRandomColumn(grp, cat, tbl, col);
        result.replace(fullMatch, val);
    }

    return result;
}

QString MarkdownTableEngine::searchRelevantContext(const QString &query) const {
    if (query.isEmpty() || m_tables.isEmpty()) return "";

    QStringList matchedLines;
    QString queryLower = query.toLower();

    for (const TableRecord &rec : m_tables) {
        bool groupOrTableMatched = queryLower.contains(rec.group.toLower()) ||
                                   queryLower.contains(rec.category.toLower()) ||
                                   queryLower.contains(rec.tableName.toLower());

        for (const QMap<QString, QString> &row : rec.rows) {
            bool rowMatched = groupOrTableMatched;
            if (!rowMatched) {
                for (auto it = row.constBegin(); it != row.constEnd(); ++it) {
                    if (queryLower.contains(it.value().toLower()) && it.value().length() >= 2) {
                        rowMatched = true;
                        break;
                    }
                }
            }

            if (rowMatched) {
                QStringList rowItems;
                for (auto it = row.constBegin(); it != row.constEnd(); ++it) {
                    rowItems.append(QString("%1: %2").arg(it.key(), it.value()));
                }
                matchedLines.append(QString("[%1/%2/%3] %4").arg(rec.group, rec.category, rec.tableName, rowItems.join(" | ")));
            }
        }
    }

    if (matchedLines.isEmpty()) return "";

    // 重複を削除して最大5行に制限
    matchedLines.removeDuplicates();
    if (matchedLines.size() > 5) {
        matchedLines = matchedLines.mid(0, 5);
    }

    return QString("\n\n【ナレッジデータベース参照結果】\n- %1").arg(matchedLines.join("\n- "));
}
