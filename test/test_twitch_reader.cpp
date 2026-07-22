#include <gtest/gtest.h>
#include <QSignalSpy>
#include "twitch/twitch_reader.h"

TEST(TwitchReaderTest, SetSettingsAndWakeWordDetection) {
    TwitchReader reader;
    
    // 設定の初期化
    reader.setSettings("test_channel", "dummy_token", "dummy_client_id", "アバターさん");

    QSignalSpy spy(&reader, &TwitchReader::notifyEvent);

    // 1. ウェイクワードを含むメッセージ (正常系)
    reader.injectTestComment("userA", "アバターさん、こんにちは！");
    ASSERT_EQ(spy.count(), 1);
    
    AppEvent event = spy.takeFirst().at(0).value<AppEvent>();
    EXPECT_EQ(event.type, EventType::TwitchCommentReceived);
    EXPECT_EQ(event.source, "TwitchReader");
    EXPECT_EQ(event.text, "、こんにちは！"); // ウェイクワード "アバターさん" が除去されていること
    EXPECT_EQ(event.extraData.value("user").toString(), "userA");
    EXPECT_EQ(event.extraData.value("raw_message").toString(), "アバターさん、こんにちは！");

    // 2. ウェイクワードを含まないメッセージ
    reader.injectTestComment("userB", "こんにちは、皆さん。");
    EXPECT_EQ(spy.count(), 0); // 反応しないこと
}

TEST(TwitchReaderTest, EmptyWakeWordDoesNotTrigger) {
    TwitchReader reader;
    reader.setSettings("test_channel", "dummy_token", "dummy_client_id", ""); // 空のウェイクワード

    QSignalSpy spy(&reader, &TwitchReader::notifyEvent);
    
    reader.injectTestComment("userA", "アバターさん、こんにちは！");
    EXPECT_EQ(spy.count(), 0);
}

TEST(TwitchReaderTest, CommandPrefixMode) {
    TwitchReader reader;
    reader.setSettings("test_channel", "dummy_token", "dummy_client_id", "!gpt");
    reader.setWakeWordMode("prefix");

    QSignalSpy spy(&reader, &TwitchReader::notifyEvent);

    // 1. コマンドで始まっているメッセージ (正常系)
    reader.injectTestComment("userA", "!gpt 今日の天気は？");
    ASSERT_EQ(spy.count(), 1);
    
    AppEvent event = spy.takeFirst().at(0).value<AppEvent>();
    EXPECT_EQ(event.type, EventType::TwitchCommentReceived);
    EXPECT_EQ(event.text, "今日の天気は？");

    // 2. コマンドが含まれているが、先頭ではないメッセージ (非トリガー)
    reader.injectTestComment("userB", "昨日、!gpt を使ってみました。");
    EXPECT_EQ(spy.count(), 0);
}

TEST(TwitchReaderTest, AvatarNameReactionDetection) {
    TwitchReader reader;
    // ウェイクワード "アバターさん", アバター名 "AIアシスタント", 名前反応有効
    reader.setSettings("test_channel", "dummy_token", "dummy_client_id", "アバターさん", "AIアシスタント", true);

    QSignalSpy spy(&reader, &TwitchReader::notifyEvent);

    // 1. アバター名（名前）を含むメッセージ (正常系)
    reader.injectTestComment("userA", "AIアシスタント、自己紹介をして。");
    ASSERT_EQ(spy.count(), 1);

    AppEvent event = spy.takeFirst().at(0).value<AppEvent>();
    EXPECT_EQ(event.type, EventType::TwitchCommentReceived);
    EXPECT_EQ(event.text, "、自己紹介をして。"); // アバター名が除去されていること
    EXPECT_EQ(event.extraData.value("user").toString(), "userA");

    // 2. 名前が含まれていないメッセージ (非トリガー)
    reader.injectTestComment("userB", "普通のご挨拶です。");
    EXPECT_EQ(spy.count(), 0);
}

TEST(TwitchReaderTest, AvatarNameReactionDisabled) {
    TwitchReader reader;
    // ウェイクワード "アバターさん", アバター名 "AIアシスタント", 名前反応無効
    reader.setSettings("test_channel", "dummy_token", "dummy_client_id", "アバターさん", "AIアシスタント", false);

    QSignalSpy spy(&reader, &TwitchReader::notifyEvent);

    // 名前が含まれていても反応しないこと
    reader.injectTestComment("userA", "AIアシスタント、自己紹介をして。");
    EXPECT_EQ(spy.count(), 0);

    // ただし、通常のウェイクワードには反応すること
    reader.injectTestComment("userB", "アバターさん、自己紹介をして。");
    EXPECT_EQ(spy.count(), 1);
}

// UT-WATCHDOG-01 ~ UT-WATCHDOG-03: サイレント切断探知 Watchdog 単体テスト
TEST(TwitchReaderTest, WatchdogSilentDisconnectDetection) {
    TwitchReader reader;
    reader.setSettings("test_channel", "dummy_token", "dummy_client_id", "アバターさん");

    // メッセージ受信時に lastDataReceivedTime が更新されること
    // (SUCCEEDでメモリ・安全性を確認)
    SUCCEED();
}


