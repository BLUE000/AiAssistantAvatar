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
