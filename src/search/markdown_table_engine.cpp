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

    QString actualRoot = m_rootDir;
    if (!QDir(actualRoot).exists()) {
        actualRoot = QCoreApplication::applicationDirPath() + "/" + m_rootDir;
    }
    if (!QDir(actualRoot).exists()) {
#ifdef PROJECT_SOURCE_DIR
        actualRoot = QString(PROJECT_SOURCE_DIR) + "/" + m_rootDir;
#endif
    }

    QDir rootDir(actualRoot);
    if (!rootDir.exists()) {
        qDebug() << "MarkdownTableEngine: Knowledge root directory does not exist:" << actualRoot;
        return;
    }

    m_rootDir = rootDir.absolutePath();
    scanDirectory(m_rootDir, "", "");
    qDebug() << "MarkdownTableEngine: Loaded" << m_tables.size() << "tables from knowledge repository.";
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

    bool inTable = false;
    QStringList headers;

    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines.at(i);
        if (line.startsWith("|") && line.endsWith("|")) {
            // パイプ区切りの行
            QStringList cells;
            QStringList rawCells = line.split("|");
            for (int c = 1; c < rawCells.size() - 1; ++c) {
                cells.append(rawCells.at(c).trimmed());
            }

            // 区切り線 (|:---|:---|) の判定
            bool isSeparator = true;
            for (const QString &cell : cells) {
                QString cleaned = cell;
                cleaned.remove(':').remove('-');
                if (!cleaned.isEmpty()) {
                    isSeparator = false;
                    break;
                }
            }

            if (isSeparator) {
                inTable = true;
                continue;
            }

            if (!inTable) {
                // ハイフン区切り線の直前行をヘッダーとみなす
                headers = cells;
                record.headers = headers;
            } else {
                // データ行
                if (cells.size() == headers.size()) {
                    QMap<QString, QString> rowMap;
                    for (int h = 0; h < headers.size(); ++h) {
                        rowMap[headers.at(h)] = cells.at(h);
                    }
                    record.rows.append(rowMap);
                }
            }
        } else {
            inTable = false;
        }
    }

    if (!record.rows.isEmpty()) {
        m_tables.append(record);
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
