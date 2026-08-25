#include "markdown_table_engine.h"
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QCoreApplication>
#include <QDate>


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
        QString appDirKnowledge = QCoreApplication::applicationDirPath() + "/knowledge";
        if (QDir(appDirKnowledge).exists()) {
            actualRoot = appDirKnowledge;
        } else if (QDir("knowledge").exists()) {
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

        QJsonArray excludeArr;
        for (const QString &ex : entry.excludeTriggers) {
            excludeArr.append(ex);
        }
        entryObj["exclude_triggers"] = excludeArr;

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
    int highestScore = -1;

    for (const KnowledgeIndexEntry &entry : m_indexEntries) {
        if (!entry.isValid) continue;

        // 除外トリガー（ネガティブキーワード）の判定: 一致する除外ワードがあれば即スキップ
        bool excluded = false;
        for (const QString &exTrig : entry.excludeTriggers) {
            if (exTrig.isEmpty()) continue;
            if (triggerWord.contains(exTrig, Qt::CaseInsensitive)) {
                excluded = true;
                break;
            }
        }
        if (excluded) continue;

        int entryScore = 0;
        int maxTrigLen = 0;
        int matchCount = 0;

        for (const QString &trig : entry.triggers) {
            if (trig.isEmpty()) continue;
            if (trig.compare(triggerWord, Qt::CaseInsensitive) == 0) {
                // 完全一致
                entryScore += 1000;
                matchCount++;
                maxTrigLen = qMax(maxTrigLen, trig.length());
            } else if (triggerWord.contains(trig, Qt::CaseInsensitive)) {
                // 部分一致（文字数に応じた重み）
                entryScore += trig.length() * 10;
                matchCount++;
                maxTrigLen = qMax(maxTrigLen, trig.length());
            }
        }

        if (matchCount > 0) {
            entryScore += (matchCount * 50); // マッチ件数ボーナス
            entryScore += entry.priority;    // 優先度加算

            if (entryScore > highestScore) {
                highestScore = entryScore;
                bestEntry = entry;
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
    bool inExcludeTriggerSection = false;
    bool inPrioritySection = false;
    bool inModeSection = false;

    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines.at(i);
        if (line.isEmpty()) continue;

        // セクション判定 (# トリガー, # 除外トリガー, # 優先度, # 処理モード)
        if (line.startsWith("#")) {
            QString headerText = line.section('#', 1).trimmed();
            inExcludeTriggerSection = (headerText.contains("除外トリガー", Qt::CaseInsensitive) || headerText.contains("除外", Qt::CaseInsensitive) || headerText.contains("Exclude", Qt::CaseInsensitive));
            inTriggerSection = !inExcludeTriggerSection && (headerText.contains("トリガー", Qt::CaseInsensitive) || headerText.contains("Trigger", Qt::CaseInsensitive));
            inPrioritySection = (headerText.contains("優先度", Qt::CaseInsensitive) || headerText.contains("Priority", Qt::CaseInsensitive));
            inModeSection = (headerText.contains("処理", Qt::CaseInsensitive) || headerText.contains("Mode", Qt::CaseInsensitive));
            continue;
        }

        if (inExcludeTriggerSection && (line.startsWith("-") || line.startsWith("*"))) {
            QString exTrig = line.mid(1).trimmed();
            if (!exTrig.isEmpty()) indexEntry.excludeTriggers.append(exTrig);
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

QString MarkdownTableEngine::selectDailyColumn(const QString &group, const QString &category, const QString &table, const QString &targetColumn, const QString &seed) const {
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
    quint32 hashVal = qHash(seed);
    int idx = static_cast<int>(hashVal % candidates.size());
    return candidates.at(idx);
}

QString MarkdownTableEngine::parseAndEvaluate(const QString &text, const QString &user) const {
    if (text.isEmpty()) return text;
    QString result = text;

    // 0. プレースホルダーの事前置換 ({Date}, {User})
    QString todayStr = QDate::currentDate().toString("yyyy-MM-dd");
    result.replace("{Date}", todayStr);
    result.replace("{User}", user);

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

    // 2. DailyTableSelect の置換 (4引数: グループ, テーブル, カラム, シード または 5引数: グループ, カテゴリ, テーブル, カラム, シード)
    QRegularExpression dailyTable5Regex("DailyTableSelect\\(\\s*\"([^\"]*)\"\\s*,\\s*\"([^\"]*)\"\\s*,\\s*\"([^\"]*)\"\\s*,\\s*\"([^\"]*)\"\\s*,\\s*\"([^\"]*)\"\\s*\\)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator d5It = dailyTable5Regex.globalMatch(result);
    while (d5It.hasNext()) {
        QRegularExpressionMatch m = d5It.next();
        QString fullMatch = m.captured(0);
        QString grp = m.captured(1);
        QString cat = m.captured(2);
        QString tbl = m.captured(3);
        QString col = m.captured(4);
        QString seed = m.captured(5);

        QString val = selectDailyColumn(grp, cat, tbl, col, seed);
        result.replace(fullMatch, val);
    }

    QRegularExpression dailyTable4Regex("DailyTableSelect\\(\\s*\"([^\"]*)\"\\s*,\\s*\"([^\"]*)\"\\s*,\\s*\"([^\"]*)\"\\s*,\\s*\"([^\"]*)\"\\s*\\)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator d4It = dailyTable4Regex.globalMatch(result);
    while (d4It.hasNext()) {
        QRegularExpressionMatch m = d4It.next();
        QString fullMatch = m.captured(0);
        QString grp = m.captured(1);
        QString tbl = m.captured(2);
        QString col = m.captured(3);
        QString seed = m.captured(4);

        QString val = selectDailyColumn(grp, "", tbl, col, seed);
        result.replace(fullMatch, val);
    }

    // 3. TableSelectRandom の置換 (3引数: グループ, テーブル, カラム または 4引数: グループ, カテゴリ, テーブル, カラム)
    QRegularExpression rand4Regex("TableSelectRandom\\(\\s*\"([^\"]*)\"\\s*,\\s*\"([^\"]*)\"\\s*,\\s*\"([^\"]*)\"\\s*,\\s*\"([^\"]*)\"\\s*\\)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator r4It = rand4Regex.globalMatch(result);
    while (r4It.hasNext()) {
        QRegularExpressionMatch m = r4It.next();
        QString fullMatch = m.captured(0);
        QString grp = m.captured(1);
        QString cat = m.captured(2);
        QString tbl = m.captured(3);
        QString col = m.captured(4);

        QString val = selectRandomColumn(grp, cat, tbl, col);
        result.replace(fullMatch, val);
    }

    QRegularExpression rand3Regex("TableSelectRandom\\(\\s*\"([^\"]*)\"\\s*,\\s*\"([^\"]*)\"\\s*,\\s*\"([^\"]*)\"\\s*\\)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator r3It = rand3Regex.globalMatch(result);
    while (r3It.hasNext()) {
        QRegularExpressionMatch m = r3It.next();
        QString fullMatch = m.captured(0);
        QString grp = m.captured(1);
        QString tbl = m.captured(2);
        QString col = m.captured(3);

        QString val = selectRandomColumn(grp, "", tbl, col);
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
