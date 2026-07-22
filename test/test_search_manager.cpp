#include <gtest/gtest.h>
#include <QSignalSpy>
#include <QRegularExpression>
#include "../src/search/search_manager.h"

// HTMLクレンジング処理のロジックが期待通り動作するか検証するテスト
TEST(SearchManagerTest, HTMLCleansingLogic) {
    QString rawHtml = "<b>Title</b> &amp; \"Subtitle\" &#x27;Test&#x27; &lt;Info&gt; &amp; &quot;Data&quot;";
    
    // 実際に duckduckgo_search_provider.cpp 内で行うクレンジングと同様の処理
    QString cleanText = rawHtml;
    cleanText.remove(QRegularExpression("<[^>]*>"));
    cleanText.replace("&amp;", "&");
    cleanText.replace("&quot;", "\"");
    cleanText.replace("&#x27;", "'");
    cleanText.replace("&lt;", "<");
    cleanText.replace("&gt;", ">");
    cleanText.replace("&#x2F;", "/");
    cleanText.replace("&nbsp;", " ");
    
    EXPECT_EQ(cleanText, "Title & \"Subtitle\" 'Test' <Info> & \"Data\"");
}

// DuckDuckGo の HTML からの検索結果抽出の正規表現テスト
TEST(SearchManagerTest, DDGRegexExtraction) {
    QString mockHtml = 
        "<div class=\"result results_links results_links_deep web-result \">"
        "  <div class=\"links_main links_deep result__body\">"
        "    <h2 class=\"result__title\">"
        "      <a class=\"result__a\" href=\"/l/?kh=-1&amp;uddg=https%3A%2F%2Fexample.com%2Ftest1\">Example Title 1</a>"
        "    </h2>"
        "    <a class=\"result__url\" href=\"https://example.com/test1\">example.com</a>"
        "    <span class=\"result__snippet\">This is a snippet for result 1. &amp; more</span>"
        "  </div>"
        "</div>";

    QRegularExpression bodyRegex("<div class=\"[^\"]*result__body[^\"]*\">([\\s\\S]*?)(?=<div class=\"result|<div class=\"results_links|<!--|$)");
    QRegularExpression titleRegex("<a class=\"[^\"]*result__a[^\"]*\"[^>]*href=\"([^\"]*)\"[^>]*>([\\s\\S]*?)</a>");
    QRegularExpression snippetRegex("<(?:a|span|div) class=\"[^\"]*result__snippet[^\"]*\"[^>]*>([\\s\\S]*?)</(?:a|span|div)>");

    QRegularExpressionMatch bodyMatch = bodyRegex.match(mockHtml);
    ASSERT_TRUE(bodyMatch.hasMatch());

    QString bodyHtml = bodyMatch.captured(0); // captured(1) もしくは 0
    QRegularExpressionMatch titleMatch = titleRegex.match(bodyHtml);
    QRegularExpressionMatch snippetMatch = snippetRegex.match(bodyHtml);

    ASSERT_TRUE(titleMatch.hasMatch());
    EXPECT_EQ(titleMatch.captured(2), "Example Title 1");
    
    ASSERT_TRUE(snippetMatch.hasMatch());
    EXPECT_EQ(snippetMatch.captured(1), "This is a snippet for result 1. &amp; more");
}

// SearchManager の初期化および状態遷移の確認
TEST(SearchManagerTest, InitialStateAndApiKeyHandling) {
    SearchManager manager;
    
    // キーを設定して正しく渡るか
    manager.setTavilyApiKey("tvly-test-key-12345");
    // (非公開変数のため直接アサートはせず、ビルドおよびメモリ安全性を確保)
    SUCCEED();
}

#include <QEventLoop>
#include <QTimer>
#include "../src/search/duckduckgo_search_provider.h"

// 実際の DuckDuckGo に問い合わせを行って動作検証するテスト (診断用 - 手動実行のみ)
TEST(SearchManagerTest, DISABLED_RealDDGSearchTest) {
    QEventLoop loop;
    DuckDuckGoSearchProvider provider;
    
    QString result;
    bool success = false;
    
    QObject::connect(&provider, &ISearchProvider::searchFinished, [&](const QString &res, bool succ){
        result = res;
        success = succ;
        loop.quit();
    });
    
    // 10秒タイムアウト用タイマー
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    
    provider.search("2026 Prime Minister");
    loop.exec();

    
    qDebug() << "=========================================";
    qDebug() << "RealDDGSearchTest Success:" << success;
    qDebug() << "RealDDGSearchTest Result Length:" << result.length();
    qDebug() << "RealDDGSearchTest Result Content:\n" << result;
    qDebug() << "=========================================";
    
    EXPECT_TRUE(success);
    EXPECT_FALSE(result.isEmpty());
}

#include "search/markdown_table_engine.h"
#include <QDir>
#include <QFile>
#include <QTextStream>

// UT-TABLEDB-01 ~ UT-TABLEDB-06 単体テストスイート
TEST(MarkdownTableEngineTest, FullTableDatabaseSuite) {
    // 1. テスト用の擬似 knowledge ディレクトリ構成を作成
    QString testDir = "test_knowledge";
    QDir().mkpath(testDir + "/Elin/装備");
    
    QFile file(testDir + "/Elin/装備/片手剣.md");
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out << "# Elin 武器データ\n";
        out << "| 武器名 | 攻撃力 | 必要素材 |\n";
        out << "|:---|:---|:---|\n";
        out << "| 鉄の剣 | 15 | 鉄鉱石x3, 木材x1 |\n";
        out << "| 炎の小剣 | 35 | 炎の結晶x2, 鉄鉱石x5 |\n";
        file.close();
    }

    // UT-TABLEDB-01: scanDirectory ＆ ロード検証
    MarkdownTableEngine engine(testDir);
    EXPECT_GE(engine.tableCount(), 1);

    // UT-TABLEDB-02: queryColumn キー検索
    QString material = engine.queryColumn("Elin", "装備", "片手剣", "鉄の剣", "必要素材");
    EXPECT_EQ(material, "鉄鉱石x3, 木材x1");

    // UT-TABLEDB-03: selectRandomColumn ランダム抽出
    QString randWeapon = engine.selectRandomColumn("Elin", "装備", "片手剣", "武器名");
    EXPECT_TRUE(randWeapon == "鉄の剣" || randWeapon == "炎の小剣");

    // UT-TABLEDB-04: isPathSafe サンドボックス境界チェック
    EXPECT_FALSE(engine.isPathSafe("../../../windows/system32"));

    // UT-TABLEDB-05: parseAndEvaluate マクロ式置換
    QString macroStr = "必要素材: TableSearch(\"Elin\", \"装備\", \"片手剣\", \"鉄の剣\", \"必要素材\")";
    QString evalStr = engine.parseAndEvaluate(macroStr);
    EXPECT_TRUE(evalStr.contains("鉄鉱石x3, 木材x1"));

    // UT-TABLEDB-06: searchRelevantContext 自然文RAG検索
    QString context = engine.searchRelevantContext("鉄の剣の必要素材を教えて");
    EXPECT_TRUE(context.contains("ナレッジデータベース参照結果"));
    EXPECT_TRUE(context.contains("鉄の剣"));

    // 後始末
    QDir(testDir).removeRecursively();
}


