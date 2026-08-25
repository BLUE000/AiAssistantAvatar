#pragma once
#include <QString>
#include <QList>
#include <QMap>
#include <QDir>
#include <QFileInfo>

#include <QJsonObject>
#include <QJsonArray>

struct TableRecord {
    QString group;                  // 情報グループ名 (最上位フォルダ)
    QString category;               // カテゴリ名 (サブフォルダ)
    QString tableName;              // テーブル名 / ファイル名 (拡張子除く)
    QString filePath;               // フルパス
    QStringList headers;            // カラム名リスト
    QList<QMap<QString, QString>> rows; // 各行のキーバリューデータ
};

struct KnowledgeIndexEntry {
    QString filePath;
    QString group;
    QString category;
    QString tableName;
    QString title;
    int priority = 100;
    QString mode = "random_row"; // "random_row" または "table_search"
    QStringList triggers;
    QStringList excludeTriggers; // 除外トリガー（ネガティブキーワード）
    QStringList headers;
    bool isValid = true;
    QString errorMessage;
    int errorLine = 0;
};

class MarkdownTableEngine {
public:
    explicit MarkdownTableEngine(const QString &rootDir = "knowledge");
    ~MarkdownTableEngine();

    // 指定ルートフォルダ配下の全マークダウンテーブルをロード・インデックス化
    void reload();

    // インデックス構築とエラー診断バリデーション (knowledge_index.json 生成)
    bool buildIndexAndValidate(QJsonObject &outIndexData, QList<KnowledgeIndexEntry> &outDiagnostics);

    // トリガーキーワードの一致判定および最高優先度 (priority) エントリーの自動解決
    KnowledgeIndexEntry resolveBestEntryForTrigger(const QString &triggerWord) const;

    // 最新の構文診断レポート（エラー・警告一覧）の取得
    QList<KnowledgeIndexEntry> diagnostics() const { return m_diagnostics; }

    // キー検索による特定カラムの数値/テキスト抽出
    // 例: queryColumn("Elin", "装備", "片手剣", "鉄の剣", "必要素材")
    QString queryColumn(const QString &group, const QString &category, const QString &table, const QString &searchKey, const QString &targetColumn) const;

    // 指定テーブルからのランダム1件抽出
    // 例: selectRandomColumn("Elin", "装備", "片手剣", "武器名")
    QString selectRandomColumn(const QString &group, const QString &category, const QString &table, const QString &targetColumn) const;

    // 指定テーブルからの決定論的日替わり1件抽出
    // 例: selectDailyColumn("Omikuji", "", "Ranks", "運勢", "2026-08-15_Taro")
    QString selectDailyColumn(const QString &group, const QString &category, const QString &table, const QString &targetColumn, const QString &seed) const;

    // テキスト内の "{Date}", "{User}", "DailyTableSelect(...)", "TableSelectRandom(...)", "TableSearch(...)" マクロ式を自動パース・評価・置換
    QString parseAndEvaluate(const QString &text, const QString &user = "") const;


    // 自然文クエリ（例: "鉄の剣の必要素材は？"）から関連するテーブルデータ行を検索し、AIシステムコンテキスト文字列を生成
    QString searchRelevantContext(const QString &query) const;

    // ロードされているテーブル数の取得（テスト用）
    int tableCount() const { return m_tables.size(); }

    // パスのサンドボックスチェック (knowledge/ ルート外への抜け出し防止)
    bool isPathSafe(const QString &path) const;

private:
    QString m_rootDir;
    QList<TableRecord> m_tables;
    QList<KnowledgeIndexEntry> m_indexEntries;
    QList<KnowledgeIndexEntry> m_diagnostics;

    void scanDirectory(const QString &dirPath, const QString &currentGroup, const QString &currentCategory);
    void parseMarkdownFile(const QString &filePath, const QString &group, const QString &category);
};
