#include <gtest/gtest.h>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "observer/community_observer_engine.h"
#include "ai/ai_client_manager.h"

class CommunityObserverTest : public ::testing::Test {
protected:
    void SetUp() override {
        testLogsDir = "test_observer_logs";
        QDir(testLogsDir).removeRecursively();
        QDir().mkpath(testLogsDir);
    }

    void TearDown() override {
        QDir(testLogsDir).removeRecursively();
    }

    QString testLogsDir;
};

// UT-OBS-01: ログ記録・追記
TEST_F(CommunityObserverTest, RecordMessageIncreasesLogCount) {
    CommunityObserverEngine engine(testLogsDir);
    EXPECT_TRUE(engine.recordMessage("twitch", "alice", "こんにちは！"));
    EXPECT_TRUE(engine.recordMessage("twitch", "alice", "ゲーム楽しい"));

    QJsonObject userObj = engine.inspectUser("twitch", "alice");
    EXPECT_EQ(userObj.value("user").toString(), "alice");
    EXPECT_EQ(userObj.value("total_records").toInt(), 2);
    QJsonArray records = userObj.value("records").toArray();
    EXPECT_EQ(records.size(), 2);
    EXPECT_EQ(records[0].toObject().value("text").toString(), "こんにちは！");
    EXPECT_EQ(records[1].toObject().value("text").toString(), "ゲーム楽しい");
}

// UT-OBS-02: 通常発言判定 (Normal)
TEST_F(CommunityObserverTest, NormalSpeechReturnsNormalStatus) {
    CommunityObserverEngine engine(testLogsDir);
    // 初期履歴の登録
    engine.recordMessage("twitch", "bob", "こんにちは！");
    engine.recordMessage("twitch", "bob", "このゲーム面白いね");
    engine.recordMessage("twitch", "bob", "ナイスプレイ！");

    ObserverEvaluationResult result = engine.evaluateMessage("twitch", "bob", "次のマッチも頑張ろう！");
    EXPECT_EQ(result.status, ObserverStatus::Normal);
    EXPECT_EQ(result.statusString, "Normal");
    EXPECT_EQ(result.concernLevel, 0);
    EXPECT_TRUE(result.directive.isEmpty());
}

// UT-OBS-03: 乖離・違和感判定 (DrasticChange)
TEST_F(CommunityObserverTest, DrasticToneChangeTriggersDrasticChangeStatus) {
    CommunityObserverEngine engine(testLogsDir);
    // 普段はポジティブ・温和
    engine.recordMessage("twitch", "charlie", "草");
    engine.recordMessage("twitch", "charlie", "ゲーム最高！面白い！");
    engine.recordMessage("twitch", "charlie", "ありがとう！楽しいね");
    engine.recordMessage("twitch", "charlie", "ナイスエイム！すごい！");

    // 突然、強い否定・棘のある発言
    ObserverEvaluationResult result = engine.evaluateMessage("twitch", "charlie", "〇〇さん本当に苦手で無理");
    EXPECT_EQ(result.status, ObserverStatus::DrasticChange);
    EXPECT_EQ(result.statusString, "DrasticChange");
    EXPECT_EQ(result.concernLevel, 2);
    EXPECT_TRUE(result.directive.contains("何かあった？") || result.directive.contains("どうしたの？"));
}

// UT-OBS-04: 継続的不満判定 (PersistentConcern)
TEST_F(CommunityObserverTest, PersistentNegativeRemarksTriggerPersistentConcern) {
    CommunityObserverEngine engine(testLogsDir);
    // 過去ログに不満が連続して蓄積
    engine.recordMessage("twitch", "dave", "〇〇さん苦手なんだよね");
    engine.recordMessage("twitch", "dave", "あいつイライラする");
    engine.recordMessage("twitch", "dave", "また〇〇さんか、うざい");

    // 再度不満発言
    ObserverEvaluationResult result = engine.evaluateMessage("twitch", "dave", "本当に腹立つし消えてほしい");
    EXPECT_EQ(result.status, ObserverStatus::PersistentConcern);
    EXPECT_EQ(result.statusString, "PersistentConcern");
    EXPECT_EQ(result.concernLevel, 3);
    EXPECT_TRUE(result.directive.contains("前にも") || result.directive.contains("無理してない？"));
}

// UT-OBS-05: ログローテーションとクリーンアップ
TEST_F(CommunityObserverTest, VacuumLogsCleansOldRecords) {
    CommunityObserverEngine engine(testLogsDir);
    // 105 件のメッセージを投入
    for (int i = 0; i < 105; ++i) {
        engine.recordMessage("twitch", "eve", QString("メッセージ %1").arg(i));
    }

    QJsonObject userObj = engine.inspectUser("twitch", "eve");
    // 最大100件に自動制限されていること
    EXPECT_LE(userObj.value("total_records").toInt(), 100);

    // vacuum 実行
    int cleaned = engine.vacuumLogs(60, 50); // 最大50件に制限
    EXPECT_GE(cleaned, 1);

    QJsonObject userObjAfter = engine.inspectUser("twitch", "eve");
    EXPECT_LE(userObjAfter.value("total_records").toInt(), 50);
}

// UT-OBS-06: AIClientManager との連携およびフォールバック
TEST_F(CommunityObserverTest, AIClientManagerEvaluatesObserverDirective) {
    AIClientManager manager;
    // 正常な通常発言
    QString dir1 = manager.evaluateWithObserver("twitch", "frank", "こんにちは！");
    EXPECT_TRUE(dir1.isEmpty());

    // 過去ログにポジティブを蓄積してから違和感発言
    CommunityObserverEngine engine("Config/observer_logs");
    for (int i = 0; i < 5; ++i) {
        engine.recordMessage("twitch", "grace", "面白い！楽しい！最高！");
    }

    QString dir2 = manager.evaluateWithObserver("twitch", "grace", "〇〇さん本当に苦手で無理");
    EXPECT_FALSE(dir2.isEmpty());
    EXPECT_TRUE(dir2.contains("対話誘導指示"));
}
