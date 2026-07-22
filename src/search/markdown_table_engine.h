#pragma once
#include <QString>
#include <QList>
#include <QMap>
#include <QDir>
#include <QFileInfo>

struct TableRecord {
    QString group;                  // 情報グループ名 (最上位フォルダ)
    QString category;               // カテゴリ名 (サブフォルダ)
    QString tableName;              // テーブル名 / ファイル名 (拡張子除く)
    QString filePath;               // フルパス
    QStringList headers;            // カラム名リスト
    QList<QMap<QString, QString>> rows; // 各行のキーバリューデータ
};

class MarkdownTableEngine {
public:
    explicit MarkdownTableEngine(const QString &rootDir = "knowledge");
    ~MarkdownTableEngine();

    // 指定ルートフォルダ配下の全マークダウンテーブルをロード・インデックス化
    void reload();

    // キー検索による特定カラムの数値/テキスト抽出
    // 例: queryColumn("Elin", "装備", "片手剣", "鉄の剣", "必要素材")
    QString queryColumn(const QString &group, const QString &category, const QString &table, const QString &searchKey, const QString &targetColumn) const;

    // 指定テーブルからのランダム1件抽出
    // 例: selectRandomColumn("Elin", "装備", "片手剣", "武器名")
    QString selectRandomColumn(const QString &group, const QString &category, const QString &table, const QString &targetColumn) const;

    // テキスト内の "TableSearch(...)" や "TableSelectRandom(...)" マクロ式を自動パース・評価・置換
    QString parseAndEvaluate(const QString &text) const;

    // 自然文クエリ（例: "鉄の剣の必要素材は？"）から関連するテーブルデータ行を検索し、AIシステムコンテキスト文字列を生成
    QString searchRelevantContext(const QString &query) const;

    // ロードされているテーブル数の取得（テスト用）
    int tableCount() const { return m_tables.size(); }

    // パスのサンドボックスチェック (knowledge/ ルート外への抜け出し防止)
    bool isPathSafe(const QString &path) const;

private:
    QString m_rootDir;
    QList<TableRecord> m_tables;

    void scanDirectory(const QString &dirPath, const QString &currentGroup, const QString &currentCategory);
    void parseMarkdownFile(const QString &filePath, const QString &group, const QString &category);
};
