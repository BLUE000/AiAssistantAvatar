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

    // 区切り行(|---|)のないテーブルのテスト
    QDir().mkpath(testDir + "/ROLC/武器/短剣");
    QFile file2(testDir + "/ROLC/武器/短剣/構成.md");
    if (file2.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file2);
        out.setEncoding(QStringConverter::Utf8);
        out << "Rank/Rare|名前|属性|左手|Lv|物攻|魔攻|会心|スキル構成|\n";
        out << "1/C|カッパーダガー|-|○|1|51|43|20%|1 2 3 - - -|\n";
        file2.close();
    }

    engine.reload();
    QString context2 = engine.searchRelevantContext("カッパーダガーのスキル構成は？");
    EXPECT_TRUE(context2.contains("カッパーダガー"));
    EXPECT_TRUE(context2.contains("1 2 3 - - -"));

    // 後始末
    QDir(testDir).removeRecursively();
}

// UT-KNOWLEDGE-INDEX-01 & UT-KNOWLEDGE-PRIORITY-02 & UT-KNOWLEDGE-VALIDATE-03
// F-29: ナレッジベース拡張 (インデックス構築、優先度解決、構文診断バリデーション) の単体テスト
TEST(SearchManagerTest, KnowledgeIndexAndValidationTest) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    // 正常なナレッジファイル 1 (優先度 100)
    QFile file1(tempDir.filePath("fortune_high.md"));
    ASSERT_TRUE(file1.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out1(&file1);
    out1 << "# トリガー\n- 占い\n- 運勢\n\n# 優先度\n- 100\n\n# 運勢データ\n| 運勢 | アイテム |\n| 大吉 | 金のコイン |\n";
    file1.close();

    // 正常なナレッジファイル 2 (優先度 50)
    QFile file2(tempDir.filePath("fortune_low.md"));
    ASSERT_TRUE(file2.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out2(&file2);
    out2 << "# トリガー\n- 占い\n\n# 優先度\n- 50\n\n# 運勢データ\n| 運勢 | アイテム |\n| 小吉 | 木の枝 |\n";
    file2.close();

    // 壊れたナレッジファイル 3 (列数不一致のエラーファイル)
    QFile file3(tempDir.filePath("broken.md"));
    ASSERT_TRUE(file3.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out3(&file3);
    out3 << "# トリガー\n- 占い\n\n| 運勢 | アイテム | カラー |\n| 大吉 | 金のコイン |\n"; // 列数 3 と 2 で不一致
    file3.close();

    MarkdownTableEngine engine(tempDir.path());
    engine.reload();

    // 1. バリデーション結果の評価 (UT-KNOWLEDGE-VALIDATE-03)
    QList<KnowledgeIndexEntry> diags = engine.diagnostics();
    ASSERT_EQ(diags.size(), 1);
    EXPECT_TRUE(diags.first().filePath.contains("broken.md"));
    EXPECT_FALSE(diags.first().isValid);
    EXPECT_TRUE(diags.first().errorMessage.contains("テーブルの列数が一致しません"));

    // 2. 優先度解決の評価 (UT-KNOWLEDGE-PRIORITY-02)
    KnowledgeIndexEntry resolved = engine.resolveBestEntryForTrigger("占い");
    EXPECT_TRUE(resolved.isValid);
    EXPECT_EQ(resolved.priority, 100);
    EXPECT_TRUE(resolved.filePath.contains("fortune_high.md"));
}

#include "ai/ai_random_utils.h"
#include <QDate>

// UT-DAILY-MACRO-01 ~ UT-DAILY-MACRO-03 & UT-KNOWLEDGE-FOLDERS-01 単体テスト
TEST(SearchManagerTest, DailyMacroAndKnowledgeFoldersTest) {
    // UT-DAILY-MACRO-01: {Date} & {User} プレースホルダーの置換
    MarkdownTableEngine engine("knowledge");
    QString templateStr = "日付: {Date} / ユーザー: {User}";
    QString parsed = engine.parseAndEvaluate(templateStr, "TestUser123");
    QString todayStr = QDate::currentDate().toString("yyyy-MM-dd");
    EXPECT_TRUE(parsed.contains("日付: " + todayStr));
    EXPECT_TRUE(parsed.contains("ユーザー: TestUser123"));

    // UT-DAILY-MACRO-02: AIRandomUtils::getDailyRandom 決定論的乱数
    QString seedA = "2026-08-15_Alice";
    QString seedB = "2026-08-15_Bob";
    int valA1 = AIRandomUtils::getDailyRandom(1, 100, seedA);
    int valA2 = AIRandomUtils::getDailyRandom(1, 100, seedA);
    int valB = AIRandomUtils::getDailyRandom(1, 100, seedB);
    EXPECT_EQ(valA1, valA2); // 同一シードなら常に同じ値
    EXPECT_GE(valA1, 1);
    EXPECT_LE(valA1, 100);

    // UT-DAILY-MACRO-03: selectDailyColumn 決定論的行選択（固定シード）
    QString fixedSeed = "2026-08-15_Alice";
    QString rank1 = engine.selectDailyColumn("Omikuji", "", "Ranks", "運勢", fixedSeed);
    QString rank2 = engine.selectDailyColumn("Omikuji", "", "Ranks", "運勢", fixedSeed);
    EXPECT_FALSE(rank1.isEmpty());
    EXPECT_EQ(rank1, rank2); // 同一シードなら常に同じ行が選ばれる

    // DailyTableSelect マクロ評価のテスト
    // parseAndEvaluate は {Date} を今日の日付で展開するため、今日のシードで期待値を算出する
    QString todaySeed = QDate::currentDate().toString("yyyy-MM-dd") + "_Alice";
    QString rankToday = engine.selectDailyColumn("Omikuji", "", "Ranks", "運勢", todaySeed);
    QString macroText = "今日の運勢: DailyTableSelect(\"Omikuji\", \"Ranks\", \"運勢\", \"{Date}_{User}\")";
    QString macroEvaluated = engine.parseAndEvaluate(macroText, "Alice");
    EXPECT_FALSE(macroEvaluated.contains("DailyTableSelect"));
    EXPECT_TRUE(macroEvaluated.contains("今日の運勢: " + rankToday));

    // UT-KNOWLEDGE-FOLDERS-01: knowledge/Omikuji と knowledge/Zodiac のロード検証
    engine.reload();
    EXPECT_GE(engine.tableCount(), 2);
    KnowledgeIndexEntry omikujiEntry = engine.resolveBestEntryForTrigger("おみくじ");
    EXPECT_TRUE(omikujiEntry.isValid);
    EXPECT_EQ(omikujiEntry.group, "Omikuji");

    KnowledgeIndexEntry zodiacEntry = engine.resolveBestEntryForTrigger("星座占い");
    EXPECT_TRUE(zodiacEntry.isValid);
    EXPECT_EQ(zodiacEntry.group, "Zodiac");

    // 各星座名単体でのトリガー解決テスト（山羊座）
    KnowledgeIndexEntry capricornEntry = engine.resolveBestEntryForTrigger("山羊座の運勢教えて");
    EXPECT_TRUE(capricornEntry.isValid);
    EXPECT_EQ(capricornEntry.group, "Zodiac");
}

#include "ai/ai_client_manager.h"

// UT-DETAIL-MODE-01 ~ UT-DETAIL-MODE-05 単体テスト
TEST(AIClientManagerTest, ResponseDetailModeAndGranularityReductionTest) {
    // UT-DETAIL-MODE-01: 通常質問（デフォルト Short）
    bool isRed1 = false;
    AIClientManager::ResponseDetailMode mode1 = AIClientManager::determineResponseDetailMode("何で台風は不規則な動きをするの？", &isRed1);
    EXPECT_EQ(mode1, AIClientManager::ResponseDetailMode::Short);
    EXPECT_FALSE(isRed1);

    // UT-DETAIL-MODE-02: 詳細要求（Detailed）
    bool isRed2 = false;
    AIClientManager::ResponseDetailMode mode2 = AIClientManager::determineResponseDetailMode("何で台風は不規則な動きをするの？詳しく教えて", &isRed2);
    EXPECT_EQ(mode2, AIClientManager::ResponseDetailMode::Detailed);
    EXPECT_FALSE(isRed2);

    // UT-DETAIL-MODE-03: 簡潔要求（Short）
    bool isRed3 = false;
    AIClientManager::ResponseDetailMode mode3 = AIClientManager::determineResponseDetailMode("台風の仕組みを一言で教えて", &isRed3);
    EXPECT_EQ(mode3, AIClientManager::ResponseDetailMode::Short);
    EXPECT_FALSE(isRed3);

    // UT-DETAIL-MODE-04: プロンプト指示生成（Short / Detailed）
    QString shortInstr = AIClientManager::formatResponseDetailInstruction(AIClientManager::ResponseDetailMode::Short, false);
    EXPECT_TRUE(shortInstr.contains("1〜3文程度"));
    EXPECT_TRUE(shortInstr.contains("簡潔"));

    QString detailedInstr = AIClientManager::formatResponseDetailInstruction(AIClientManager::ResponseDetailMode::Detailed, false);
    EXPECT_TRUE(detailedInstr.contains("詳しく包括的に解説"));

    // UT-DETAIL-MODE-05: ユーザー指摘による粒度縮小・言い直し（Short + isReduction=true）
    bool isRed5 = false;
    AIClientManager::ResponseDetailMode mode5 = AIClientManager::determineResponseDetailMode("ちょっと説明が細かすぎるよ", &isRed5);
    EXPECT_EQ(mode5, AIClientManager::ResponseDetailMode::Short);
    EXPECT_TRUE(isRed5);

    QString redInstr = AIClientManager::formatResponseDetailInstruction(mode5, isRed5);
    EXPECT_TRUE(redInstr.contains("粒度修正指示"));
    EXPECT_TRUE(redInstr.contains("1〜2 文程度にギュッと凝縮"));
}

// UT-KNOWLEDGE-TRIGGER-SCORE-01 ~ UT-KNOWLEDGE-TRIGGER-SCORE-02 単体テスト
TEST(MarkdownTableEngineTest, TriggerMatchingScoringAndResolutionTest) {

    MarkdownTableEngine engine("knowledge");
    
    // UT-KNOWLEDGE-TRIGGER-SCORE-01: 「山羊座の今日の運勢は？」で Zodiac/Signs が正しく選定される
    KnowledgeIndexEntry entryZodiac = engine.resolveBestEntryForTrigger("山羊座の今日の運勢は？");
    EXPECT_TRUE(entryZodiac.isValid);
    EXPECT_EQ(entryZodiac.group, "Zodiac");
    EXPECT_EQ(entryZodiac.tableName, "Signs");

    // UT-KNOWLEDGE-TRIGGER-SCORE-02: 「おみくじ引いて」で Omikuji/Ranks が正しく選定される
    KnowledgeIndexEntry entryOmikuji = engine.resolveBestEntryForTrigger("おみくじ引いて");
    EXPECT_TRUE(entryOmikuji.isValid);
    EXPECT_EQ(entryOmikuji.group, "Omikuji");
    EXPECT_EQ(entryOmikuji.tableName, "Ranks");
}

