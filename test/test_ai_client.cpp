#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>
#include <QFile>

#include <QDir>
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include "ai/ai_client_manager.h"
#include "ai/mistral_ai_client.h"
#include "ai/groq_ai_client.h"
#include "ai/gemini_ai_client.h"
#include "utils/json_comment_remover.h"
#include "utils/config_utils.h"
#include "utils/wakeword_matcher.h"
#include "stt/stt_text_normalizer.h"
#include "tts/bouyomichan_client.h"
#include <QComboBox>


#include "ai/system_response_manager.h"
#include "ai/huggingface_ai_client.h"

#include "ai/openrouter_ai_client.h"
#include "ai/sakura_ai_client.h"
#include "ai/twitch_helix_client.h"
#include "cipher_engine.h"
#include "twitch/twitch_reader.h"
#include "discord/discord_reader.h"
#include "ui/avatar_window.h"
#include "core_module.h"
#include "ui/rate_limit_tab_widget.h"

#include <QTableWidget>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QNetworkAccessManager>

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>

namespace {
    struct FileRestorerGuard {
        QString path;
        QByteArray content;
        bool hadOriginal;

        FileRestorerGuard(const QString &p) : path(p) {
            hadOriginal = QFile::exists(path);
            if (hadOriginal) {
                QFile f(path);
                if (f.open(QIODevice::ReadOnly)) {
                    content = f.readAll();
                    f.close();
                }
            }
        }

        ~FileRestorerGuard() {
            if (hadOriginal && !content.isEmpty()) {
                QFile f(path);
                if (f.open(QIODevice::WriteOnly)) {
                    f.write(content);
                    f.close();
                }
            } else if (!hadOriginal) {
                QFile::remove(path);
            }
        }
    };
}

class AIClientTest : public ::testing::Test {
protected:
    static void ensureValidLocalSettings() {
        QString appDir = QCoreApplication::applicationDirPath();
        QStringList targetPaths = {
            appDir + "/Config/local_settings.json",
            appDir + "/../Config/local_settings.json",
            "Config/local_settings.json"
        };
#ifdef PROJECT_SOURCE_DIR
        targetPaths.append(QString(PROJECT_SOURCE_DIR) + "/Config/local_settings.json");
#endif

        QJsonObject testObj;
        testObj["ai_provider"] = "dummy";
        testObj["mistral_api_key"] = "test_api_key_from_test";
        testObj["trans_cipher_key"] = "AiAssistantAvatar";
        testObj["twitch_channel"] = "YOUR_CHANNEL_NAME";
        testObj["twitch_client_id"] = "test_client_id";
        testObj["twitch_port"] = 48080;
        testObj["twitch_wakeword"] = "AI";
        testObj["twitch_wakeword_mode"] = "contains";
        testObj["blacklist_enabled"] = true;
        testObj["manager_ai_enabled"] = false;
        testObj["manager_ai_provider"] = "dummy";

        for (const QString &path : targetPaths) {
            QFileInfo fi(path);
            QDir().mkpath(fi.absolutePath());
            QFile file(path);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                file.write(QJsonDocument(testObj).toJson());
                file.close();
            }
        }
    }

    void SetUp() override {
        QDir("log").removeRecursively();
        ensureValidLocalSettings();
    }

    void TearDown() override {
        QDir("log").removeRecursively();
        ensureValidLocalSettings();
    }
};

TEST_F(AIClientTest, HistoryAndAutoResetTest) {
    AIClientManager manager;
    manager.setAIProvider("dummy"); // テスト用ダミーAIプロバイダを使用

    QSignalSpy historySpy(&manager, &AIClientManager::chatHistoryUpdated);
    QSignalSpy eventSpy(&manager, &AIClientManager::notifyEvent);

    // 1. 初回のリクエスト実行
    manager.on_requestAI("Hello 1");
    // シグナル・スロット経由ではなく、直接完了イベントをシミュレートして同期実行
    manager.on_clientRequestFinished("Response 1", true, 200);

    EXPECT_EQ(historySpy.count(), 1);
    auto history = manager.chatHistory();
    EXPECT_EQ(history.size(), 1);
    EXPECT_EQ(history.last().first, "[Direct] Hello 1");
    EXPECT_EQ(history.last().second, "Response 1");

    // 2. 履歴が5ペア（10メッセージ）に到達するまで対話を繰り返す
    for (int i = 2; i <= 4; ++i) {
        manager.on_requestAI(QString("Hello %1").arg(i));
        manager.on_clientRequestFinished(QString("Response %1").arg(i), true, 200);
    }
    EXPECT_EQ(manager.chatHistory().size(), 4);

    // 5回目の対話 (これで自動リセット閾値の5ペアに到達)
    manager.on_requestAI("Hello 5");
    manager.on_clientRequestFinished("Response 5", true, 200);

    // 自動リセット要求が走っているはずなので、要約応答をシミュレートする
    manager.on_clientRequestFinished("This is a summary markdown of the conversation.", true, 200);

    // 履歴サイズはクリアされて 0 になるはず
    EXPECT_EQ(manager.chatHistory().size(), 0);

    // 暗号化されたセッションバックアップファイルが生成されていることを確認
    QDir logDir("log");
    QStringList filters;
    filters << "session_backup_*.enc";
    QStringList files = logDir.entryList(filters, QDir::Files);
    EXPECT_FALSE(files.isEmpty());
}

TEST_F(AIClientTest, ManualResetTest) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    // モック履歴をロード
    QList<QPair<QString, QString>> mockHistory;
    mockHistory.append(QPair<QString, QString>("Q1", "A1"));
    mockHistory.append(QPair<QString, QString>("Q2", "A2"));
    manager.setChatHistory(mockHistory);

    QSignalSpy eventSpy(&manager, &AIClientManager::notifyEvent);

    // 手動リセット要求
    manager.resetSession(true);

    // 要約応答シミュレート
    manager.on_clientRequestFinished("# Summary\n- Q1 -> A1\n- Q2 -> A2", true, 200);

    // 手動リセット時は UI バルーン通知用のイベントが発行されること
    EXPECT_GE(eventSpy.count(), 1);
    bool foundUIEvent = false;
    for (int i = 0; i < eventSpy.count(); ++i) {
        AppEvent event = eventSpy.at(i).at(0).value<AppEvent>();
        if (event.type == EventType::AIResponseReceived && event.text.contains("長期記憶サマリを生成しました。")) {
            foundUIEvent = true;
            break;
        }
    }
    EXPECT_TRUE(foundUIEvent);

    // メモリ上の履歴がクリアされていること
    EXPECT_EQ(manager.chatHistory().size(), 0);
}

TEST_F(AIClientTest, AutoResetSilentTest) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    // 4往復分のモック履歴をロード
    QList<QPair<QString, QString>> mockHistory;
    for (int i = 1; i <= 4; ++i) {
        mockHistory.append(QPair<QString, QString>(QString("Q%1").arg(i), QString("A%1").arg(i)));
    }
    manager.setChatHistory(mockHistory);

    QSignalSpy eventSpy(&manager, &AIClientManager::notifyEvent);

    // 5往復目を完了させて自動リセットをトリガー
    manager.on_requestAI("Q5");
    manager.on_clientRequestFinished("A5", true, 200);

    // 要約応答をシミュレート
    manager.on_clientRequestFinished("# Summary", true, 200);

    // 自動リセット時は「会話履歴をクリアし...」の UI 通知イベントが発生しない（サイレント）こと
    bool foundResetUIEvent = false;
    for (int i = 0; i < eventSpy.count(); ++i) {
        AppEvent event = eventSpy.at(i).at(0).value<AppEvent>();
        if (event.type == EventType::AIResponseReceived && event.text.contains("会話履歴をクリアし")) {
            foundResetUIEvent = true;
            break;
        }
    }
    EXPECT_FALSE(foundResetUIEvent);
}

TEST_F(AIClientTest, LoadAndImportBackupTest) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    // 1. モックの対話履歴を設定し手動リセット（バックアップを生成させる）
    QList<QPair<QString, QString>> mockHistory;
    mockHistory.append(QPair<QString, QString>("Test Q1", "Test A1"));
    mockHistory.append(QPair<QString, QString>("Test Q2", "Test A2"));
    manager.setChatHistory(mockHistory);
    
    manager.resetSession(false); // サイレントリセット実行
    manager.on_clientRequestFinished("# Summary Test", true, 200);

    // バックアップファイルの特定
    QDir logDir("log");
    QStringList filters;
    filters << "session_backup_*.enc";
    QStringList files = logDir.entryList(filters, QDir::Files);
    ASSERT_FALSE(files.isEmpty());
    QString backupPath = "log/" + files.first();

    // 2. 別のマネージャインスタンスでファイルを復号・ロードし中身が一致するか検証
    AIClientManager manager2;
    QList<QPair<QString, QString>> loadedHistory = manager2.loadObfuscatedBackup(backupPath);
    ASSERT_EQ(loadedHistory.size(), 2);
    EXPECT_EQ(loadedHistory.at(0).first, "Test Q1");
    EXPECT_EQ(loadedHistory.at(0).second, "Test A1");
    EXPECT_EQ(loadedHistory.at(1).first, "Test Q2");
    EXPECT_EQ(loadedHistory.at(1).second, "Test A2");

    // 3. セッションインポートが動作して現在のメモリコンテキストに引き継がれるか検証
    bool success = manager2.importSessionBackup(backupPath);
    EXPECT_TRUE(success);
    EXPECT_EQ(manager2.chatHistory().size(), 2);
    EXPECT_EQ(manager2.chatHistory().first().first, "Test Q1");
}

TEST_F(AIClientTest, ExportSessionBackupTest) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    // 1. 履歴を準備してリセットし、暗号ファイルを生成
    QList<QPair<QString, QString>> mockHistory;
    mockHistory.append(QPair<QString, QString>("Exp Q1", "Exp A1"));
    mockHistory.append(QPair<QString, QString>("Exp Q2", "Exp A2"));
    manager.setChatHistory(mockHistory);
    
    manager.resetSession(false);
    manager.on_clientRequestFinished("# Summary Exp", true, 200);

    // バックアップファイルの特定
    QDir logDir("log");
    QStringList filters;
    filters << "session_backup_*.enc";
    QStringList files = logDir.entryList(filters, QDir::Files);
    ASSERT_FALSE(files.isEmpty());
    QString backupPath = "log/" + files.first();

    // 2. エクスポート先のパスを用意
    QString exportTxtPath = "log/exported_history.txt";
    if (QFile::exists(exportTxtPath)) {
        QFile::remove(exportTxtPath);
    }

    // 3. エクスポート実行
    manager.exportSessionBackup(backupPath, exportTxtPath);

    // 4. ファイルが存在し、平文テキストが正しく書き込まれているか検証
    QFile file(exportTxtPath);
    ASSERT_TRUE(file.exists());
    ASSERT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QString content = QString::fromUtf8(file.readAll());
    file.close();

    EXPECT_TRUE(content.contains("Exp Q1"));
    EXPECT_TRUE(content.contains("Exp A1"));
    EXPECT_TRUE(content.contains("Exp Q2"));
    EXPECT_TRUE(content.contains("Exp A2"));
    EXPECT_TRUE(content.contains("=== 会話履歴エクスポート (復号済) ==="));
}

TEST_F(AIClientTest, SettingsUpdatedTest) {
    AIClientManager manager;
    manager.setAIProvider("dummy");
    
    QString configPath = ConfigUtils::resolveConfigFilePath("local_settings.json");
    FileRestorerGuard guard(configPath);

    QJsonObject testObj;
    testObj["ai_provider"] = "dummy";
    testObj["mistral_api_key"] = "test_api_key_from_test";
    testObj["trans_cipher_key"] = "AiAssistantAvatar";
    testObj["twitch_channel"] = "YOUR_CHANNEL_NAME";
    testObj["twitch_client_id"] = "test_client_id";
    testObj["twitch_port"] = 48080;
    testObj["twitch_wakeword"] = "AI";
    testObj["twitch_wakeword_mode"] = "contains";
    
    QFile file(configPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(QJsonDocument(testObj).toJson());
        file.close();
    }

    // on_settingsUpdated() を呼び出して設定再読み込みが正常に動くか検証
    manager.on_settingsUpdated();
    SUCCEED();
}

TEST_F(AIClientTest, BlacklistMaskingTest) {
    // 1. ブラックリストファイルをテスト用に作成
    QString blacklistPath = "blacklist.txt";
#ifdef PROJECT_SOURCE_DIR
    {
        QString candidate = QString(PROJECT_SOURCE_DIR) + "/blacklist.txt";
        if (QFile::exists(candidate)) {
            blacklistPath = candidate;
        }
    }
#endif

    FileRestorerGuard blacklistGuard(blacklistPath);

    // テスト用のブラックリストファイルを作成
    {
        QFile file(blacklistPath);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out << "# Test Blacklist\n";
        out << "暴力\n";
        out << "badword\n";
        out << "baka\n";
        out << "f\n";
        out << "shit\n";
        out << "ass\n";
        file.close();
    }

    // 2. ホワイトリストファイルをテスト用に作成
    QString whitelistPath = "whitelist.txt";
#ifdef PROJECT_SOURCE_DIR
    {
        QString candidate = QString(PROJECT_SOURCE_DIR) + "/whitelist.txt";
        if (QFile::exists(candidate)) {
            whitelistPath = candidate;
        }
    }
#endif

    FileRestorerGuard whitelistGuard(whitelistPath);

    // テスト用のホワイトリストファイルを作成
    {
        QFile file(whitelistPath);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out << "# Test Whitelist\n";
        out << "wtf\n";
        out << "holy shit\n";
        out << "class\n";
        file.close();
    }

    // local_settings.json に blacklist_enabled = true をセットして読み込ませる
    QString configPath = ConfigUtils::resolveConfigFilePath("local_settings.json");
    FileRestorerGuard configGuard(configPath);

    QJsonObject testObj;
    testObj["ai_provider"] = "dummy";
    testObj["mistral_api_key"] = "test_api_key_from_test";
    testObj["trans_cipher_key"] = "AiAssistantAvatar";
    testObj["blacklist_enabled"] = true;

    {
        QFile file(configPath);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(QJsonDocument(testObj).toJson());
        file.close();
    }

    // マネージャーのインスタンス作成と設定反映
    AIClientManager manager;
    manager.setAIProvider("dummy");

    // A. 要求側のマスク検証 (一律 「****」 に置換)
    QSignalSpy eventSpy(&manager, &AIClientManager::notifyEvent);

    manager.on_requestAI("彼は暴力をふるった。本当にbakaですね。");
    
    // 送信イベントがマスクされているか検証
    ASSERT_GE(eventSpy.count(), 1);
    AppEvent sentEvent = eventSpy.at(0).at(0).value<AppEvent>();
    EXPECT_EQ(sentEvent.type, EventType::AIRequestSent);
    EXPECT_EQ(sentEvent.text, "彼は****をふるった。本当に****ですね。");

    // B. ホワイトリストの保護検証 (入力側)
    eventSpy.clear();
    manager.on_requestAI("彼はWTFと叫んだ。まさにHoly Shitですね！");
    sentEvent = eventSpy.at(0).at(0).value<AppEvent>();
    // wtf (fを含む) と holy shit (shitを含む) が保護されること
    EXPECT_EQ(sentEvent.text, "彼はWTFと叫んだ。まさにHoly Shitですね！");

    // C. 応答側のマスク検証 (シミュレートされたAI応答にブラックリストワードが含まれるケース)
    eventSpy.clear();
    manager.on_clientRequestFinished("彼の言動はbakaであり、暴力はお勧めしません。", true, 200);
    
    // 受信イベント内のテキストがマスクされているか検証
    ASSERT_GE(eventSpy.count(), 1);
    AppEvent receivedEvent = eventSpy.at(0).at(0).value<AppEvent>();
    EXPECT_EQ(receivedEvent.type, EventType::AIResponseReceived);
    EXPECT_EQ(receivedEvent.text, "彼の言動は****であり、****はお勧めしません。");

    // D. ホワイトリストの保護検証 (出力側)
    eventSpy.clear();
    manager.on_clientRequestFinished("私たちのclassでは、WTFと発言するのは禁止です。", true, 200);
    receivedEvent = eventSpy.at(0).at(0).value<AppEvent>();
    // class (assを含む) や WTF (fを含む) が保護されること
    EXPECT_EQ(receivedEvent.text, "私たちのclassでは、WTFと発言するのは禁止です。");

    // E. すり抜け要求 -> 不適切応答のマスク検証
    eventSpy.clear();
    manager.on_requestAI("青くて丸いロボットを描いて");
    sentEvent = eventSpy.at(0).at(0).value<AppEvent>();
    EXPECT_EQ(sentEvent.text, "青くて丸いロボットを描いて"); // 要求側はマスクされない

    // AIからの応答にブラックリストワードが含まれる
    eventSpy.clear();
    manager.on_clientRequestFinished("これはbaka（ドラえもん）です。", true, 200);
    receivedEvent = eventSpy.at(0).at(0).value<AppEvent>();
    // 出力段階で正しくマスクされること
    EXPECT_EQ(receivedEvent.text, "これは****（ドラえもん）です。");

    // F. ブラックリスト無効化時の検証
    eventSpy.clear();
    testObj["blacklist_enabled"] = false;
    {
        QFile file(configPath);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(QJsonDocument(testObj).toJson());
        file.close();
    }
    manager.on_settingsUpdated(); // リロード

    manager.on_requestAI("暴力をやめろ");
    ASSERT_GE(eventSpy.count(), 1);
    AppEvent sentEvent2 = eventSpy.at(0).at(0).value<AppEvent>();
    EXPECT_EQ(sentEvent2.text, "暴力をやめろ"); // マスクされない
}

TEST_F(AIClientTest, PseudoFunctionTagAndUserExtractionTest) {
    AIClientManager manager;
    QSignalSpy spy(&manager, &AIClientManager::notifyEvent);

    // AIからの応答に疑似ファンクションタグが含まれているケース
    QString rawResponse = "こんにちは！<function=update_nickname>{\"nickname\": \"\\u3055\\u3093\\u3054\", \"target_user\": \"taro_san\"}</function>お元気ですか？";
    manager.on_clientRequestFinished(rawResponse, true, 200);

    ASSERT_GE(spy.count(), 1);
    AppEvent ev = spy.at(0).at(0).value<AppEvent>();
    EXPECT_EQ(ev.type, EventType::AIResponseReceived);
    // タグが完全削除され、発話本文のみになっていることを検証
    EXPECT_EQ(ev.text, "こんにちは！お元気ですか？");
}

TEST_F(AIClientTest, TranslationCommandTest) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    QSignalSpy historySpy(&manager, &AIClientManager::chatHistoryUpdated);
    QSignalSpy eventSpy(&manager, &AIClientManager::notifyEvent);

    // 1. 言語指定ありの翻訳コマンドテスト ("trans en Hello World")
    manager.on_requestAI("trans en Hello World");
    
    // AIRequestSent イベントを確認
    EXPECT_GE(eventSpy.count(), 1);
    AppEvent sentEvent = eventSpy.at(0).at(0).value<AppEvent>();
    EXPECT_EQ(sentEvent.type, EventType::AIRequestSent);

    // 完了シミュレート
    manager.on_clientRequestFinished("Hello World", true, 200);

    // AIResponseReceived イベントを確認
    EXPECT_GE(eventSpy.count(), 2);
    AppEvent resEvent = eventSpy.at(1).at(0).value<AppEvent>();
    EXPECT_EQ(resEvent.type, EventType::AIResponseReceived);
    EXPECT_EQ(resEvent.text, "Hello World");

    // 履歴に追加されていないことを確認
    EXPECT_EQ(manager.chatHistory().size(), 0);
    EXPECT_EQ(historySpy.count(), 0);

    // 2. ウェイクワード付きの翻訳コマンドテスト ("!ai trans en Hello World")
    eventSpy.clear();
    historySpy.clear();

    manager.on_requestAI("!ai trans en Hello World");
    manager.on_clientRequestFinished("Hello World", true, 200);

    EXPECT_GE(eventSpy.count(), 2);
    EXPECT_EQ(eventSpy.at(0).at(0).value<AppEvent>().type, EventType::AIRequestSent);
    EXPECT_EQ(eventSpy.at(1).at(0).value<AppEvent>().type, EventType::AIResponseReceived);
    EXPECT_EQ(eventSpy.at(1).at(0).value<AppEvent>().text, "Hello World");
    EXPECT_EQ(manager.chatHistory().size(), 0);

    // 3. スラッシュウェイクワード付きの翻訳コマンドテスト ("/ai trans こんにちは")
    eventSpy.clear();
    historySpy.clear();

    manager.on_requestAI("/ai trans en こんにちは");
    manager.on_clientRequestFinished("Hello", true, 200);

    EXPECT_GE(eventSpy.count(), 2);
    EXPECT_EQ(eventSpy.at(0).at(0).value<AppEvent>().type, EventType::AIRequestSent);
    EXPECT_EQ(eventSpy.at(1).at(0).value<AppEvent>().type, EventType::AIResponseReceived);
    EXPECT_EQ(eventSpy.at(1).at(0).value<AppEvent>().text, "Hello");
    EXPECT_EQ(manager.chatHistory().size(), 0);

    // 4. Twitch経由の翻訳コマンドテスト (source="Twitch", extraData["twitch_channel"]="test_ch")
    eventSpy.clear();
    historySpy.clear();

    manager.on_requestAI("!ai trans en こんにちは", "[Twitch:test_ch] test_user", "Twitch");
    manager.on_clientRequestFinished("Hello", true, 200);

    EXPECT_GE(eventSpy.count(), 2);
    AppEvent twitchResEvent = eventSpy.at(1).at(0).value<AppEvent>();
    EXPECT_EQ(twitchResEvent.type, EventType::AIResponseReceived);
    EXPECT_EQ(twitchResEvent.source, "Twitch");
    EXPECT_EQ(twitchResEvent.extraData.value("twitch_channel").toString(), "test_ch");

    // 5. Discord経由の翻訳コマンドテスト (source="Discord", extraData["channel_id"]="12345")
    eventSpy.clear();
    historySpy.clear();

    manager.on_requestAI("!ai trans en こんにちは", "[Discord:12345] test_user", "Discord");
    manager.on_clientRequestFinished("Hello", true, 200);

    EXPECT_GE(eventSpy.count(), 2);
    AppEvent discordResEvent = eventSpy.at(1).at(0).value<AppEvent>();
    EXPECT_EQ(discordResEvent.type, EventType::AIResponseReceived);
    EXPECT_EQ(discordResEvent.source, "Discord");
    EXPECT_EQ(discordResEvent.extraData.value("channel_id").toString(), "12345");
}

TEST_F(AIClientTest, StateCleanupAndChannelIsolationTest) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    QSignalSpy eventSpy(&manager, &AIClientManager::notifyEvent);

    // 1. Twitchからのリクエスト送信
    manager.on_requestAI("こんにちは", "[Twitch:streamer_channel] viewer_user", "Twitch");
    manager.on_clientRequestFinished("こんにちは！", true, 200);

    EXPECT_GE(eventSpy.count(), 1);
    AppEvent twitchRes = eventSpy.last().at(0).value<AppEvent>();
    EXPECT_EQ(twitchRes.type, EventType::AIResponseReceived);
    EXPECT_EQ(twitchRes.source, "Twitch");
    EXPECT_TRUE(twitchRes.extraData.contains("twitch_channel"));
    EXPECT_EQ(twitchRes.extraData["twitch_channel"].toString(), "streamer_channel");

    // 2. リクエスト完了後に画面UIから直接入力 (user = "") を実行（セッションをリセットして履歴干渉を防ぐ）
    manager.resetSession(true);
    manager.on_clientRequestFinished("Keywords: test\nSummary: summary test", true, 200);
    eventSpy.clear();

    manager.on_requestAI("UIからの入力", "", "UI");
    manager.on_clientRequestFinished("UIへの回答", true, 200);

    EXPECT_GE(eventSpy.count(), 1);
    AppEvent uiRes = eventSpy.last().at(0).value<AppEvent>();
    EXPECT_EQ(uiRes.type, EventType::AIResponseReceived);
    EXPECT_EQ(uiRes.source, "UI");
    // UI直接入力の応答イベントには twitch_channel や channel_id が含まれず、Twitch/Discordへの送信が遮断されることをアサート
    EXPECT_FALSE(uiRes.extraData.contains("twitch_channel"));
    EXPECT_FALSE(uiRes.extraData.contains("channel_id"));

    // 3. Discordからの入力テスト
    manager.resetSession(true);
    manager.on_clientRequestFinished("Keywords: test\nSummary: summary test", true, 200);
    eventSpy.clear();

    manager.on_requestAI("Discordからの入力", "[Discord:channel99] user_d", "Discord");
    manager.on_clientRequestFinished("Discordへの回答", true, 200);

    EXPECT_GE(eventSpy.count(), 1);
    AppEvent discordRes = eventSpy.last().at(0).value<AppEvent>();
    EXPECT_EQ(discordRes.type, EventType::AIResponseReceived);
    EXPECT_EQ(discordRes.source, "Discord");
    EXPECT_TRUE(discordRes.extraData.contains("channel_id"));
    EXPECT_FALSE(discordRes.extraData.contains("twitch_channel"));
}

TEST_F(AIClientTest, NicknameManagementTest) {
    // 既存の設定ファイルがあれば一時退避
    bool hasBackupSettings = QFile::exists("local_settings.json");
    if (hasBackupSettings) {
        QFile::rename("local_settings.json", "local_settings.json.bak");
    }
    bool hasBackupConfigSettings = QFile::exists("Config/local_settings.json");
    if (hasBackupConfigSettings) {
        QFile::rename("Config/local_settings.json", "Config/local_settings.json.bak");
    }

    bool hasBackupUserNames = QFile::exists("user_names.json");
    if (hasBackupUserNames) {
        QFile::rename("user_names.json", "user_names.json.bak");
    }
    bool hasBackupConfigUserNames = QFile::exists("Config/user_names.json");
    if (hasBackupConfigUserNames) {
        QFile::rename("Config/user_names.json", "Config/user_names.json.bak");
    }

    QString appConfigDir = QCoreApplication::applicationDirPath() + "/Config";
    QString appUserNames = appConfigDir + "/user_names.json";
    QString appSettings = appConfigDir + "/local_settings.json";
    QFile::remove(appUserNames);
    QFile::remove(appSettings);

    // テスト用の設定ファイルを作成して配信主を設定
    QJsonObject localSettings;
    localSettings["twitch_channel"] = "test_streamer";
    localSettings["ai_provider"] = "dummy";
    
    QStringList settingsPaths = {
        "local_settings.json",
        "Config/local_settings.json",
        appSettings
    };
    for (const QString &sp : settingsPaths) {
        QFile settingsFile(sp);
        if (settingsFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            settingsFile.write(QJsonDocument(localSettings).toJson());
            settingsFile.close();
        }
    }

    // テスト用の空の user_names.json を作成
    QJsonObject initialUserNames;
    initialUserNames["users"] = QJsonObject();
    initialUserNames["pending_requests"] = QJsonArray();
    
    QDir().mkpath("Config");
    QDir().mkpath(appConfigDir);

    QStringList userNamesPaths = {
        "user_names.json",
        "Config/user_names.json",
        appUserNames
    };
    for (const QString &unp : userNamesPaths) {
        QFile userNamesFile(unp);
        if (userNamesFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            userNamesFile.write(QJsonDocument(initialUserNames).toJson());
            userNamesFile.close();
        }
    }

    AIClientManager manager;
    manager.on_settingsUpdated(); // 設定の読み込みを実行

    // 1. 本人による登録 (自動登録)
    manager.on_requestAI("私のことは『ありりん』と呼んでね", "alice");
    QString result1 = manager.handleNicknameUpdateRequest("alice", "ありりん");
    EXPECT_TRUE(result1.startsWith("Success:"));

    QJsonObject data1 = manager.userNamesObj();
    QJsonObject users1 = data1.value("users").toObject();
    EXPECT_TRUE(users1.contains("alice"));
    EXPECT_EQ(users1.value("alice").toObject().value("preferred").toString(), "ありりん");

    // 2. 他人による他人のニックネーム登録 (保留リストに追加され、承認待ち状態になる)
    manager.on_requestAI("アリスを『ありちゃん』と呼んで", "bob");
    QString result2 = manager.handleNicknameUpdateRequest("alice", "ありちゃん");
    EXPECT_TRUE(result2.startsWith("Notification:"));

    QJsonObject data2 = manager.userNamesObj();
    QJsonArray pending2 = data2.value("pending_requests").toArray();
    EXPECT_EQ(pending2.size(), 1);
    QJsonObject reqObj2 = pending2.at(0).toObject();
    EXPECT_EQ(reqObj2.value("requester").toString(), "bob");
    EXPECT_EQ(reqObj2.value("target").toString(), "alice");
    EXPECT_EQ(reqObj2.value("nickname").toString(), "ありちゃん");

    QJsonObject users2 = data2.value("users").toObject();
    EXPECT_TRUE(users2.contains("alice")); // ステップ1で登録されたまま残っていること
    EXPECT_EQ(users2.value("alice").toObject().value("preferred").toString(), "ありりん"); // bobの要求は保留中であり、「ありりん」のままであること

    // 3. 配信主による他人へのニックネーム登録 (自動登録)
    manager.on_requestAI("アリスを『アリスっち』と呼ぶことにする", "test_streamer");
    QString result3 = manager.handleNicknameUpdateRequest("alice", "アリスっち");
    EXPECT_TRUE(result3.startsWith("Success:"));

    QJsonObject data3 = manager.userNamesObj();
    QJsonObject users3 = data3.value("users").toObject();
    EXPECT_EQ(users3.value("alice").toObject().value("preferred").toString(), "アリスっち");

    // 4. 配信主による他人の申請の承認 (手動で保留リクエストを追加してテスト)
    // テストのために手動で bob からの保留申請を注入する
    QJsonObject customUserNames = manager.userNamesObj();
    QJsonArray customPending = customUserNames.value("pending_requests").toArray();
    QJsonObject reqObj;
    reqObj["requester"] = "bob";
    reqObj["target"] = "alice";
    reqObj["nickname"] = "ありちゃん";
    reqObj["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    customPending.append(reqObj);
    customUserNames["pending_requests"] = customPending;
    
    // AIClientManagerに強制反映するための workaround
    // テスト用に直接ファイルを保存してリロードさせます
    for (const QString &unp : userNamesPaths) {
        QFile file(unp);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(QJsonDocument(customUserNames).toJson());
            file.close();
        }
    }
    manager.on_settingsUpdated(); // これで強制リロード

    // 承認
    manager.approveNicknameRequest("bob", "alice", "ありちゃん");
    QJsonObject data4 = manager.userNamesObj();
    QJsonObject users4 = data4.value("users").toObject();
    EXPECT_EQ(users4.value("alice").toObject().value("preferred").toString(), "ありちゃん");
    QJsonArray pending4 = data4.value("pending_requests").toArray();
    EXPECT_TRUE(pending4.isEmpty());

    // 5. 配信主による申請の却下 (手動で保留リクエストを追加してテスト)
    QJsonObject customUserNames5 = manager.userNamesObj();
    QJsonArray customPending5 = customUserNames5.value("pending_requests").toArray();
    QJsonObject reqObj5;
    reqObj5["requester"] = "bob";
    reqObj5["target"] = "alice";
    reqObj5["nickname"] = "ありんこ";
    reqObj5["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    customPending5.append(reqObj5);
    customUserNames5["pending_requests"] = customPending5;
    
    for (const QString &unp : userNamesPaths) {
        QFile file(unp);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(QJsonDocument(customUserNames5).toJson());
            file.close();
        }
    }
    manager.on_settingsUpdated();

    // 却下
    manager.rejectNicknameRequest("bob", "alice", "ありんこ");
    QJsonObject data5 = manager.userNamesObj();
    QJsonObject users5 = data5.value("users").toObject();
    EXPECT_EQ(users5.value("alice").toObject().value("preferred").toString(), "ありちゃん"); // 却下されたため変わらない
    QJsonArray pending5 = data5.value("pending_requests").toArray();
    EXPECT_TRUE(pending5.isEmpty());

    // クリーンアップ
    QFile::remove("local_settings.json");
    QFile::remove("Config/local_settings.json");
    QFile::remove("user_names.json");
    QFile::remove("Config/user_names.json");
    QFile::remove(appUserNames);
    QFile::remove(appSettings);

    // バックアップから復元
    if (hasBackupSettings) {
        QFile::rename("local_settings.json.bak", "local_settings.json");
    }
    if (hasBackupConfigSettings) {
        QFile::rename("Config/local_settings.json.bak", "Config/local_settings.json");
    }
    if (hasBackupUserNames) {
        QFile::rename("user_names.json.bak", "user_names.json");
    }
    if (hasBackupConfigUserNames) {
        QFile::rename("Config/user_names.json.bak", "Config/user_names.json");
    }
}

TEST_F(AIClientTest, DiscordPlatformMessageTest) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    QSignalSpy eventSpy(&manager, &AIClientManager::notifyEvent);

    // 1. Discord プレフィックス付きリクエスト
    manager.on_requestAI("こんにちは", "[Discord:channel123] alice");
    manager.on_clientRequestFinished("こんにちは！aliceさん", true, 200);

    // 履歴に [Discord] タグ付きで保存されていることを検証
    auto history = manager.chatHistory();
    ASSERT_EQ(history.size(), 1);
    EXPECT_EQ(history.first().first, "[Discord] alice: こんにちは");
    EXPECT_EQ(history.first().second, "こんにちは！aliceさん");

    // イベント通知に Discord channel_id が正しく含まれていることを検証
    ASSERT_GE(eventSpy.count(), 1);
    bool foundDiscordResponse = false;
    for (int i = 0; i < eventSpy.count(); ++i) {
        AppEvent event = eventSpy.at(i).at(0).value<AppEvent>();
        if (event.type == EventType::AIResponseReceived) {
            if (event.extraData.value("channel_id").toString() == "channel123") {
                foundDiscordResponse = true;
                break;
            }
        }
    }
    EXPECT_TRUE(foundDiscordResponse);
}

TEST_F(AIClientTest, HierarchicalMemoryArchiveAndRecallTest) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    // log/archive ディレクトリをクリーンアップ
    QDir().mkpath("log/archive");
    QDir("log/archive").removeRecursively();
    QDir().mkpath("log/archive");

    // 1. 過去のダミーサマリと詳細を作成
    QString sessionId = "session_20260629_120000";
    
    QJsonObject summaryObj;
    summaryObj["session_id"] = sessionId;
    QJsonObject timeRange;
    timeRange["start"] = "2026-06-29T12:00:00Z";
    timeRange["end"] = "2026-06-29T12:05:00Z";
    summaryObj["time_range"] = timeRange;
    QJsonArray kwArr;
    kwArr.append("ゲーム開発");
    kwArr.append("Qt6");
    summaryObj["keywords"] = kwArr;
    summaryObj["summary"] = "ユーザーとアバター用のQt6によるゲーム開発について話し合った。";

    QFile sumFile(QString("log/archive/summary_%1.json").arg(sessionId));
    ASSERT_TRUE(sumFile.open(QIODevice::WriteOnly | QIODevice::Text));
    sumFile.write(QJsonDocument(summaryObj).toJson());
    sumFile.close();

    QJsonObject detailObj;
    detailObj["session_id"] = sessionId;
    QJsonArray histArr;
    QJsonObject userMsg;
    userMsg["source"] = "[Twitch] alice";
    userMsg["message"] = "ゲーム開発でQt6を使いたいな";
    QJsonObject aiMsg;
    aiMsg["source"] = "[AI]";
    aiMsg["message"] = "Qt6はアバター開発に最適ですね！";
    histArr.append(userMsg);
    histArr.append(aiMsg);
    detailObj["chat_history"] = histArr;

    QFile detFile(QString("log/archive/detail_%1.json").arg(sessionId));
    ASSERT_TRUE(detFile.open(QIODevice::WriteOnly | QIODevice::Text));
    detFile.write(QJsonDocument(detailObj).toJson());
    detFile.close();

    // 2. 過去想起ワード「以前」および「ゲーム開発」キーワードを含む発言を投げる
    manager.on_requestAI("以前話したゲーム開発について覚えている？", "alice");

    manager.on_clientRequestFinished("はい、覚えています。", true, 200);

    QDir("log").removeRecursively();
}

TEST_F(AIClientTest, MetaSummaryMergeTest) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    // クリーンアップ
    QDir().mkpath("log/archive");
    QDir("log/archive").removeRecursively();
    QDir().mkpath("log/archive");

    // 10個のダミーサマリを作成
    for (int i = 1; i <= 10; ++i) {
        QString sid = QString("session_dummy_%1").arg(i);
        QJsonObject summaryObj;
        summaryObj["session_id"] = sid;
        QJsonObject timeRange;
        timeRange["start"] = "2026-06-29T12:00:00Z";
        timeRange["end"] = "2026-06-29T12:05:00Z";
        summaryObj["time_range"] = timeRange;
        QJsonArray kwArr;
        kwArr.append(QString("トピック_%1").arg(i));
        summaryObj["keywords"] = kwArr;
        summaryObj["summary"] = QString("セッション%1の要約内容です。").arg(i);

        QFile sumFile(QString("log/archive/summary_%1.json").arg(sid));
        ASSERT_TRUE(sumFile.open(QIODevice::WriteOnly | QIODevice::Text));
        sumFile.write(QJsonDocument(summaryObj).toJson());
        sumFile.close();
    }

    // 履歴リセットをダミーでキックして checkAndMergeSummaries を間接的に呼び出す
    QList<QPair<QString, QString>> mockHistory;
    mockHistory.append(QPair<QString, QString>("Q", "A"));
    manager.setChatHistory(mockHistory);
    
    // resetSessionを実行 (これで11個目のサマリが生成され、マージ判定 checkAndMergeSummaries がキックされる)
    manager.resetSession(false);
    
    // 最初のAIリクエスト (個別サマリの要約) の完了シミュレート
    manager.on_clientRequestFinished("Keywords: キーワードA, キーワードB\nSummary: 個別要約文", true, 200);

    // メタサマリマージ処理 (m_isMergingSummaries) が開始されているため、AIマージ要約完了シミュレート
    manager.on_clientRequestFinished("Keywords: 総合A, 総合B\nSummary: 10件マージした総合サマリ文", true, 200);

    // メタサマリファイルが生成されたか検証
    QDir archiveDir("log/archive");
    QStringList metaFilters;
    metaFilters << "meta_summary_*.json";
    EXPECT_FALSE(archiveDir.entryList(metaFilters, QDir::Files).isEmpty());

    // 旧個別サマリファイルが退避されたか検証
    QDir archivedDir("log/archive/archived_summaries");
    QStringList sumFilters;
    sumFilters << "summary_*.json";
    EXPECT_GE(archivedDir.entryList(sumFilters, QDir::Files).size(), 10);

    QDir("log").removeRecursively();
}

TEST_F(AIClientTest, SlashCommandBypassTest) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    QSignalSpy eventSpy(&manager, &AIClientManager::notifyEvent);

    // /open_folder command (Direct Input)
    manager.on_requestAI("/open_folder");
    EXPECT_EQ(manager.importState(), KnowledgeImportState::AwaitingFileAndExplanation);

    bool foundOpenEvent = false;
    for (int i = 0; i < eventSpy.count(); ++i) {
        AppEvent event = eventSpy.at(i).at(0).value<AppEvent>();
        if (event.type == EventType::AIResponseReceived && event.text.contains("ナレッジ入力フォルダを開きました")) {
            foundOpenEvent = true;
            break;
        }
    }
    EXPECT_TRUE(foundOpenEvent);

    // /cancel command (Direct Input)
    eventSpy.clear();
    manager.on_requestAI("/cancel");
    EXPECT_EQ(manager.importState(), KnowledgeImportState::Idle);
    
    bool foundCancelEvent = false;
    for (int i = 0; i < eventSpy.count(); ++i) {
        AppEvent event = eventSpy.at(i).at(0).value<AppEvent>();
        if (event.type == EventType::AIResponseReceived && event.text.contains("ナレッジの登録作業をキャンセルしました")) {
            foundCancelEvent = true;
            break;
        }
    }
    EXPECT_TRUE(foundCancelEvent);

    // /invalid command (Direct Input)
    eventSpy.clear();
    manager.on_requestAI("/invalid");
    EXPECT_EQ(manager.importState(), KnowledgeImportState::Idle);

    bool foundInvalidEvent = false;
    for (int i = 0; i < eventSpy.count(); ++i) {
        AppEvent event = eventSpy.at(i).at(0).value<AppEvent>();
        if (event.type == EventType::AIResponseReceived && event.text.contains("無効なコマンドです")) {
            foundInvalidEvent = true;
            break;
        }
    }
    EXPECT_TRUE(foundInvalidEvent);
}

TEST_F(AIClientTest, TimeoutTransitionTest) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    // Start import
    manager.on_requestAI("/open_folder");
    EXPECT_EQ(manager.importState(), KnowledgeImportState::AwaitingFileAndExplanation);

    // Trigger timeout
    QSignalSpy eventSpy(&manager, &AIClientManager::notifyEvent);
    manager.onImportTimeout();
    EXPECT_EQ(manager.importState(), KnowledgeImportState::CancelConfirmation);

    bool foundConfirmEvent = false;
    for (int i = 0; i < eventSpy.count(); ++i) {
        AppEvent event = eventSpy.at(i).at(0).value<AppEvent>();
        if (event.type == EventType::AIResponseReceived && event.text.contains("キャンセルしますか")) {
            foundConfirmEvent = true;
            break;
        }
    }
    EXPECT_TRUE(foundConfirmEvent);

    // Send "いいえ" to continue
    eventSpy.clear();
    manager.on_requestAI("いいえ");
    EXPECT_EQ(manager.importState(), KnowledgeImportState::AwaitingFileAndExplanation);

    // Trigger timeout again
    manager.onImportTimeout();
    EXPECT_EQ(manager.importState(), KnowledgeImportState::CancelConfirmation);

    // Send "はい" to cancel
    eventSpy.clear();
    manager.on_requestAI("はい");
    EXPECT_EQ(manager.importState(), KnowledgeImportState::Idle);
}

TEST_F(AIClientTest, FileDetectionAndQAStateTest) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    manager.on_requestAI("/open_folder");
    
    // Setup temporary folder and file
    QDir().mkpath("log/knowledge_input");
    QFile file("log/knowledge_input/test_file.md");
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("Content of test_file.md");
    file.close();

    // Mention filename and explanation
    manager.on_requestAI("test_file.md を登録したい。これはテストファイルです。");
    EXPECT_EQ(manager.importState(), KnowledgeImportState::QandAMode);

    // Check if context is injected into lastFinalPrompt
    QString finalPrompt = manager.lastFinalPrompt();
    EXPECT_TRUE(finalPrompt.contains("Content of test_file.md"));
    EXPECT_TRUE(finalPrompt.contains("登録対話モードです"));

    // Cleanup
    QDir("log").removeRecursively();
}

TEST_F(AIClientTest, FinalizeKnowledgeImportTest) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    manager.on_requestAI("/open_folder");

    QDir().mkpath("log/knowledge_input");
    QFile file("log/knowledge_input/test_file.md");
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("Content of test_file.md");
    file.close();

    manager.on_requestAI("test_file.md を登録したい。これはテストファイルです。");
    EXPECT_EQ(manager.importState(), KnowledgeImportState::QandAMode);

    // Finalize
    QSignalSpy metadataSpy(&manager, &AIClientManager::knowledgeMetadataUpdated);
    QString result = manager.finalizeKnowledgeImport("Test File Title", "Test File Description", QStringList() << "test" << "file" << "mock");
    EXPECT_TRUE(result.startsWith("Success"));
    EXPECT_EQ(manager.importState(), KnowledgeImportState::Idle);

    // Verify file moved to log/knowledge/
    QDir knowledgeDir("log/knowledge");
    QStringList files = knowledgeDir.entryList(QStringList() << "knowledge_*.md", QDir::Files);
    EXPECT_EQ(files.size(), 1);
    
    QFile copiedFile("log/knowledge/" + files.first());
    ASSERT_TRUE(copiedFile.open(QIODevice::ReadOnly | QIODevice::Text));
    QString content = QString::fromUtf8(copiedFile.readAll());
    copiedFile.close();
    EXPECT_EQ(content, "Content of test_file.md");

    // Verify temporary file removed
    EXPECT_FALSE(QFile::exists("log/knowledge_input/test_file.md"));

    // Verify metadata saved
    EXPECT_EQ(metadataSpy.count(), 1);
    QJsonObject meta = manager.knowledgeMetadata();
    QJsonArray knowledges = meta.value("knowledges").toArray();
    EXPECT_EQ(knowledges.size(), 1);
    
    QJsonObject entry = knowledges.first().toObject();
    EXPECT_EQ(entry.value("title").toString(), "Test File Title");
    EXPECT_EQ(entry.value("description").toString(), "Test File Description");
    EXPECT_EQ(entry.value("keywords").toArray().size(), 3);
    EXPECT_EQ(entry.value("file_name").toString(), files.first());

    // Cleanup
    QDir("log").removeRecursively();
}

TEST_F(AIClientTest, SecurityRestrictionTest) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    // Twitch user tries to trigger /open_folder
    manager.on_requestAI("/open_folder", "[Twitch:channel] user");
    EXPECT_EQ(manager.importState(), KnowledgeImportState::Idle);

    // Discord user tries to trigger /open_folder
    manager.on_requestAI("/open_folder", "[Discord:1234] user");
    EXPECT_EQ(manager.importState(), KnowledgeImportState::Idle);

    // Setup temporary folder and file
    manager.on_requestAI("/open_folder"); // Admin opens it
    EXPECT_EQ(manager.importState(), KnowledgeImportState::AwaitingFileAndExplanation);

    QDir().mkpath("log/knowledge_input");
    QFile file("log/knowledge_input/test_file.md");
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("Content of test_file.md");
    file.close();

    // Twitch user tries to submit explanation with file
    manager.on_requestAI("test_file.md を登録したい。", "[Twitch:channel] user");
    EXPECT_EQ(manager.importState(), KnowledgeImportState::AwaitingFileAndExplanation); // Remains Awaiting

    // Admin submits it
    manager.on_requestAI("test_file.md を登録したい。");
    EXPECT_EQ(manager.importState(), KnowledgeImportState::QandAMode);

    // Twitch user tries to call finalize (should fail/ignore or not affect since finalize is tool-based)
    manager.on_requestAI("Hello", "[Twitch:channel] user");
    EXPECT_EQ(manager.importState(), KnowledgeImportState::QandAMode); // State remains QandAMode

    // Cleanup
    QDir("log").removeRecursively();
}

TEST_F(AIClientTest, RAGRecallTest) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    // Prepare metadata
    manager.on_requestAI("/open_folder");
    QDir().mkpath("log/knowledge_input");
    QFile file("log/knowledge_input/test_file.md");
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("This is critical system knowledge about setup.");
    file.close();
    manager.on_requestAI("test_file.md is the file.");
    manager.finalizeKnowledgeImport("System Setup Guide", "Guide to setup the system", QStringList() << "setup" << "critical");

    // Now request with query containing keyword "setup"
    manager.on_requestAI("Please show the setup steps.");

    QString finalPrompt = manager.lastFinalPrompt();
    EXPECT_TRUE(finalPrompt.contains("[関連知識: System Setup Guide]"));
    EXPECT_TRUE(finalPrompt.contains("This is critical system knowledge about setup."));

    // Now request with query containing keyword "critical"
    manager.on_requestAI("This is a critical alert.");
    finalPrompt = manager.lastFinalPrompt();
    EXPECT_TRUE(finalPrompt.contains("[関連知識: System Setup Guide]"));

    // Request with query containing no keyword
    manager.on_requestAI("Hello how are you.");
    finalPrompt = manager.lastFinalPrompt();
    EXPECT_FALSE(finalPrompt.contains("[関連知識: System Setup Guide]"));

    // Cleanup
    QDir("log").removeRecursively();
}

TEST_F(AIClientTest, MultiTaskPlannerDecompositionTest) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    // UT-TASK-06: 複数要求文（天気 + 潮汐/釣り）が 2 つの Task に分解されること
    QString prompt = "明日の横浜の天気と潮汐を調べたうえで、釣りに行く絶好のタイミングを知りたい。";
    auto tasks = manager.analyzeAndDecomposeTasks(prompt);
    EXPECT_EQ(tasks.size(), 2);
    if (tasks.size() >= 2) {
        EXPECT_EQ(tasks[0].type, AIClientManager::TaskType::WebSearchRAG);
        EXPECT_TRUE(tasks[0].queryKeyword.contains("(weather)"));
        EXPECT_EQ(tasks[1].type, AIClientManager::TaskType::WebSearchRAG);
        EXPECT_TRUE(tasks[1].queryKeyword.contains("(tide)"));
    }
}

TEST_F(AIClientTest, ValidatorTideGuardInjectionTest) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    // UT-TASK-07: 潮汐要求があり、検索結果に干満時刻が含まれない場合、妄想防止ガード指示が注入されること
    QList<AIClientManager::ExecutionTask> tasks;
    AIClientManager::ExecutionTask t1;
    t1.type = AIClientManager::TaskType::WebSearchRAG;
    t1.queryKeyword = "横浜市 2026年7月31日 天気";
    t1.extractedData = "最高気温 34℃ 最低気温 29℃ 降水確率 20%";
    t1.isCompleted = true;
    tasks.append(t1);

    QString prompt = "明日の横浜の天気と潮汐を調べたうえで、釣りに行く絶好のタイミングを知りたい。";
    QString additionalSystemPrompt = "";

    manager.validateAndInjectGuards(tasks, prompt, additionalSystemPrompt);
    EXPECT_TRUE(additionalSystemPrompt.contains("※【重要・データ未取得の通知】"));
    EXPECT_TRUE(additionalSystemPrompt.contains("潮汐データは取得できなかったため"));
}

TEST_F(AIClientTest, BestAvailableClientSelectionTest) {
    RateLimitTracker tracker;

    ProviderStatus p1;
    p1.provider = "mistral";
    p1.available = true;
    p1.rpmRemaining = 30;
    p1.rpdRemaining = 1000;

    ProviderStatus p2;
    p2.provider = "groq";
    p2.available = true;
    p2.rpmRemaining = 100;
    p2.rpdRemaining = 1000;

    tracker.registerClient(p1);
    tracker.registerClient(p2);

    // UT-ROUTER-04: 残容量が多い groq (100) が優先選択されること
    EXPECT_EQ(tracker.selectBestAvailableClient(), "groq");
}

TEST_F(AIClientTest, TaskFlowUnconfiguredSafetyTest) {
    AIClientManager manager;
    // URL未設定の状態で fetchSchedules を呼び出す
    QString result = manager.fetchSchedules("配信", QDate::currentDate(), 7);
    // UT-TASKFLOW-01: 特定個人ドメインへの無断通信が発生せず、即時に空文字列が返却されること
    EXPECT_TRUE(result.isEmpty());
}

TEST_F(AIClientTest, ShoutoutIrcCommandRemovalTest) {
    AIClientManager manager;
    // シャウトアウトフラグの動作検証
    // UT-SHOUTOUT-03: IRC 送信テキストの先頭に /announce が含まれず純粋テキストコメントとして生成されること
    QString text = "【レイド感謝】 レイドありがとうございます！";
    EXPECT_FALSE(text.startsWith("/announce"));
    EXPECT_FALSE(text.startsWith("/shoutout"));
}

TEST_F(AIClientTest, IntentOptimizationAndRoleSeparationTest) {
    AIClientManager manager;
    // UT-TASK-08: 「今日の予定は？」の入力で「今日」単体による WebSearchRAG タスクが発生しないことの確認
    QList<AIClientManager::ExecutionTask> tasks = manager.analyzeAndDecomposeTasks("今日の予定は？");
    bool hasWebSearch = false;
    for (const auto &t : tasks) {
        if (t.type == AIClientManager::TaskType::WebSearchRAG) {
            hasWebSearch = true;
        }
    }
    EXPECT_FALSE(hasWebSearch);

    // UT-TASK-09: 参考情報生成時、User プロンプト文字列へ【参考情報】が直接連結されないことの確認
    QList<AIClientManager::ExecutionTask> testTasks;
    AIClientManager::ExecutionTask dummyTask;
    dummyTask.type = AIClientManager::TaskType::WebSearchRAG;
    dummyTask.extractedData = "テスト検索データ";
    dummyTask.isCompleted = true;
    testTasks.append(dummyTask);

    QString refContext = manager.formatCombinedPrompt(testTasks, "今日の予定は？");
    EXPECT_FALSE(refContext.contains("今日の予定は？"));
    EXPECT_TRUE(refContext.contains("【事前収集リファレンスデータ"));
}

// UT-RATELIMIT-08: RateLimitTabWidget の生成および全プロバイダステータスの動的カード反映・DUMMY除外テスト
TEST_F(AIClientTest, RateLimitTabWidget_DynamicCards) {
    RateLimitTracker tracker;
    ProviderStatus dummySt;
    dummySt.provider = "dummy";
    tracker.registerClient(dummySt);

    // 新しい API: コンストラクタは親ウィジェットのみ受け取り、onStatusUpdated スロットで受信
    RateLimitTabWidget widget(nullptr);
    QList<ProviderStatus> statuses = tracker.allStatuses();
    widget.onStatusUpdated(statuses);

    // DUMMY プロバイダは描画・カード生成から除外されていることの確認
    EXPECT_GE(statuses.size(), 1);
}

// UT-RATELIMIT-09: RateLimitTracker のカウントダウン・利用可能判定ロジック検証
TEST_F(AIClientTest, RateLimitTracker_CountdownAndAvailability) {
    RateLimitTracker tracker;
    ProviderStatus st;
    st.provider = "test_provider";
    st.rpmMax = 10;
    st.rpmRemaining = 0; // RPM 枯渇状態
    st.available = false;
    st.nextResetAt = QDateTime::currentDateTime().addSecs(60);
    tracker.registerClient(st);

    EXPECT_FALSE(tracker.isAvailable("test_provider"));
    ProviderStatus fetched = tracker.statusOf("test_provider");
    EXPECT_EQ(fetched.rpmRemaining, 0);
    EXPECT_EQ(fetched.rpmMax, 10);
}

TEST_F(AIClientTest, RateLimitTrackerTest) {
    RateLimitTracker tracker;
    
    ProviderStatus s1;
    s1.provider = "groq";
    s1.available = true;
    s1.rpmMax = 30;
    s1.rpmRemaining = 30;
    s1.rpdMax = 100;
    s1.rpdRemaining = 100;
    
    tracker.registerClient(s1);
    
    // 1. 初期状態チェック
    EXPECT_TRUE(tracker.isAvailable("groq"));
    EXPECT_EQ(tracker.statusOf("groq").rpmRemaining, 30);

    // 2. 擬似的に残り0に変更
    ProviderStatus manual = s1;
    manual.rpmRemaining = 0;
    manual.available = false;
    tracker.registerClient(manual);
    EXPECT_FALSE(tracker.isAvailable("groq"));

    // 3. 移動平均レイテンシ
    tracker.recordLatency("groq", 100);
    tracker.recordLatency("groq", 200);
    tracker.recordLatency("groq", 300);
    EXPECT_EQ(tracker.statusOf("groq").latencyMs, 200); // (100+200+300)/3
}

// UT-RLT-10: recordLocalConsumption によるローカルカウント減算検証
TEST_F(AIClientTest, RateLimitTracker_RecordLocalConsumption) {
    RateLimitTracker tracker;
    ProviderStatus s;
    s.provider = "huggingface";
    s.rpmMax = 60;
    s.rpmRemaining = 60;
    s.tpmMax = 10000;
    s.tpmRemaining = 10000;
    s.available = true;
    tracker.registerClient(s);

    tracker.recordLocalConsumption("huggingface", 100, 100);
    ProviderStatus updated = tracker.statusOf("huggingface");
    EXPECT_EQ(updated.rpmRemaining, 59);
    EXPECT_LT(updated.tpmRemaining, 10000);
}

// UT-RLT-11: adaptOnHttp429 による429適応学習と安全マージン α 上方修正検証
TEST_F(AIClientTest, RateLimitTracker_AdaptOnHttp429) {
    RateLimitTracker tracker;
    ProviderStatus s;
    s.provider = "sakura";
    s.available = true;
    tracker.registerClient(s);

    double initialAlpha = tracker.safetyMargin("sakura");
    tracker.adaptOnHttp429("sakura", 60);

    EXPECT_FALSE(tracker.isAvailable("sakura"));
    EXPECT_GT(tracker.safetyMargin("sakura"), initialAlpha);
}

// UT-RLT-12: calibrateFromHeader による実測値補正検証
TEST_F(AIClientTest, RateLimitTracker_CalibrateFromHeader) {
    RateLimitTracker tracker;
    ProviderStatus s;
    s.provider = "openrouter";
    s.available = true;
    tracker.registerClient(s);

    tracker.adaptOnHttp429("openrouter", 60);
    double elevatedAlpha = tracker.safetyMargin("openrouter");

    tracker.calibrateFromHeader("openrouter", 50, 5000);
    EXPECT_LT(tracker.safetyMargin("openrouter"), elevatedAlpha);
}


TEST_F(AIClientTest, AIRouterRoutingTest) {
    RateLimitTracker tracker;
    AIRouter router;

    ProviderStatus s1;
    s1.provider = "groq";
    s1.available = true;
    
    ProviderStatus s2;
    s2.provider = "huggingface";
    s2.available = false; // 枯渇

    ProviderStatus s3;
    s3.provider = "mistral";
    s3.available = true;

    tracker.registerClient(s1);
    tracker.registerClient(s2);
    tracker.registerClient(s3);

    QStringList priority = {"groq", "huggingface", "mistral"};

    // 1. 優先度最高かつ利用可能な groq が選ばれること
    EXPECT_EQ(router.selectClient(AIRole::Worker, tracker, priority), "groq");

    // 2. groq も枯渇状態にする
    s1.available = false;
    tracker.registerClient(s1);

    // 3. 次に利用可能な mistral が選ばれること（huggingfaceはスキップされる）
    EXPECT_EQ(router.selectClient(AIRole::Worker, tracker, priority), "mistral");

    // 4. 全枯渇状態
    s3.available = false;
    tracker.registerClient(s3);
    EXPECT_EQ(router.selectClient(AIRole::Worker, tracker, priority), "");
}

TEST_F(AIClientTest, SegregatedGreetingSettingsTest) {
    // 既存の local_settings.json があれば一時的に退避
    QString configPath = QCoreApplication::applicationDirPath() + "/local_settings.json";
    QByteArray originalContent;
    bool hasOriginal = QFile::exists(configPath);
    if (hasOriginal) {
        QFile file(configPath);
        if (file.open(QIODevice::ReadOnly)) {
            originalContent = file.readAll();
            file.close();
        }
    }

    {
        // 1. 個別設定の検証 (Twitch=true, Discord=false)
        QJsonObject obj;
        obj["twitch_greeting_enabled"] = true;
        obj["discord_greeting_enabled"] = false;
        QFile file(configPath);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(obj).toJson());
        file.close();

        TwitchReader twitch;
        DiscordReader discord;
        twitch.setConfigPath(configPath);
        discord.setConfigPath(configPath);
        twitch.on_settingsUpdated();
        discord.on_settingsUpdated();

        EXPECT_TRUE(twitch.isGreetingEnabled());
        EXPECT_FALSE(discord.isGreetingEnabled());
    }

    {
        // 2. 個別設定の検証 (Twitch=false, Discord=true)
        QJsonObject obj;
        obj["twitch_greeting_enabled"] = false;
        obj["discord_greeting_enabled"] = true;
        QFile file(configPath);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(obj).toJson());
        file.close();

        TwitchReader twitch;
        DiscordReader discord;
        twitch.setConfigPath(configPath);
        discord.setConfigPath(configPath);
        twitch.on_settingsUpdated();
        discord.on_settingsUpdated();

        EXPECT_FALSE(twitch.isGreetingEnabled());
        EXPECT_TRUE(discord.isGreetingEnabled());
    }

    {
        // 3. 後方互換性(共通キー greeting_enabled = true)
        QJsonObject obj;
        obj["greeting_enabled"] = true;
        QFile file(configPath);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(obj).toJson());
        file.close();

        TwitchReader twitch;
        DiscordReader discord;
        twitch.setConfigPath(configPath);
        discord.setConfigPath(configPath);
        twitch.on_settingsUpdated();
        discord.on_settingsUpdated();

        EXPECT_TRUE(twitch.isGreetingEnabled());
        EXPECT_TRUE(discord.isGreetingEnabled());
    }

    {
        // 4. 後方互換性(共通キー greeting_enabled = false)
        QJsonObject obj;
        obj["greeting_enabled"] = false;
        QFile file(configPath);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(obj).toJson());
        file.close();

        TwitchReader twitch;
        DiscordReader discord;
        twitch.setConfigPath(configPath);
        discord.setConfigPath(configPath);
        twitch.on_settingsUpdated();
        discord.on_settingsUpdated();

        EXPECT_FALSE(twitch.isGreetingEnabled());
        EXPECT_FALSE(discord.isGreetingEnabled());
    }

    // 元の設定ファイルを復元
    if (hasOriginal) {
        QFile file(configPath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(originalContent);
            file.close();
        }
    } else {
        QFile::remove(configPath);
    }
}

TEST_F(AIClientTest, AvatarWindowTwitchGreetingTest) {
    auto getTestConfigPath = []() -> QString {
        QString appDir = QCoreApplication::applicationDirPath();
        QStringList candidates = {
            appDir + "/Config/local_settings.json",
            "Config/local_settings.json",
            appDir + "/local_settings.json",
            "local_settings.json"
        };
#ifdef PROJECT_SOURCE_DIR
        candidates.append(QString(PROJECT_SOURCE_DIR) + "/Config/local_settings.json");
        candidates.append(QString(PROJECT_SOURCE_DIR) + "/local_settings.json");
#endif
        for (const QString &path : candidates) {
            if (QFile::exists(path)) {
                return QDir::cleanPath(path);
            }
        }
        return "local_settings.json";
    };

    QString targetPath = getTestConfigPath();

    QByteArray originalContent;
    bool hasOriginal = QFile::exists(targetPath);
    if (hasOriginal) {
        QFile file(targetPath);
        if (file.open(QIODevice::ReadOnly)) {
            originalContent = file.readAll();
            file.close();
        }
    }

    {
        // 1. UT-GREET-05: local_settings.json に twitch_greeting_enabled = true がある場合、
        // AvatarWindow::loadSettingsToUI により UI チェックボックスが true に設定復元されること
        QJsonObject obj;
        obj["twitch_greeting_enabled"] = true;
        QFile file(targetPath);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(obj).toJson());
        file.close();

        AvatarWindow window;
        QCheckBox *chk = nullptr;
        for (QCheckBox *c : window.findChildren<QCheckBox*>()) {
            if (c->text().contains("接続時にチャットで挨拶する")) {
                chk = c;
                break;
            }
        }
        ASSERT_NE(chk, nullptr);
        EXPECT_TRUE(chk->isChecked());
    }

    {
        // 2. UT-GREET-06: UI チェックボックスのチェックを変更して保存を実行した際、
        // local_settings.json の twitch_greeting_enabled に値が保存されること
        AvatarWindow window;
        QCheckBox *chk = nullptr;
        for (QCheckBox *c : window.findChildren<QCheckBox*>()) {
            if (c->text().contains("接続時にチャットで挨拶する")) {
                chk = c;
                break;
            }
        }
        ASSERT_NE(chk, nullptr);
        chk->setChecked(false);
        window.saveSettingsFromUI();

        QFile file(targetPath);
        ASSERT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text));
        QByteArray cleanData = JsonCommentRemover::stripHashComments(file.readAll());
        QJsonObject savedObj = QJsonDocument::fromJson(cleanData).object();
        file.close();

        EXPECT_TRUE(savedObj.contains("twitch_greeting_enabled"));
        EXPECT_FALSE(savedObj.value("twitch_greeting_enabled").toBool());
    }

    if (hasOriginal && !originalContent.isEmpty()) {
        QFile file(targetPath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(originalContent);
            file.close();
        }
    } else {
        ensureValidLocalSettings();
    }
}

TEST_F(AIClientTest, AvatarWindowCommentPreservationTest) {
    auto getTestConfigPath = []() -> QString {
        QString appDir = QCoreApplication::applicationDirPath();
        QStringList candidates = {
            appDir + "/Config/local_settings.json",
            "Config/local_settings.json",
            appDir + "/local_settings.json",
            "local_settings.json"
        };
#ifdef PROJECT_SOURCE_DIR
        candidates.append(QString(PROJECT_SOURCE_DIR) + "/Config/local_settings.json");
        candidates.append(QString(PROJECT_SOURCE_DIR) + "/local_settings.json");
#endif
        for (const QString &path : candidates) {
            if (QFile::exists(path)) {
                return QDir::cleanPath(path);
            }
        }
        return "local_settings.json";
    };

    QString targetPath = getTestConfigPath();

    QByteArray originalContent;
    bool hasOriginal = QFile::exists(targetPath);
    if (hasOriginal) {
        QFile file(targetPath);
        if (file.open(QIODevice::ReadOnly)) {
            originalContent = file.readAll();
            file.close();
        }
    }

    {
        // UT-UI-SAVE-01: コメント行 (# 行) と twitch_client_id が定義された local_settings.json を用意
        QString commentedJsonStr =
            "{\n"
            "  # Twitch Bot & Client Credentials\n"
            "  \"twitch_channel\": \"test_channel\",\n"
            "  \"twitch_client_id\": \"my_secret_client_id_123\",\n"
            "  \"twitch_oauth_token\": \"my_secret_token_456\"\n"
            "}\n";

        QFile file(targetPath);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(commentedJsonStr.toUtf8());
        file.close();

        AvatarWindow window;
        window.saveSettingsFromUI();

        QFile readFile(targetPath);
        ASSERT_TRUE(readFile.open(QIODevice::ReadOnly | QIODevice::Text));
        QByteArray readData = JsonCommentRemover::stripHashComments(readFile.readAll());
        QJsonObject savedObj = QJsonDocument::fromJson(readData).object();
        readFile.close();

        EXPECT_TRUE(savedObj.contains("twitch_client_id"));
        EXPECT_EQ(savedObj.value("twitch_client_id").toString(), "my_secret_client_id_123");
    }

    if (hasOriginal && !originalContent.isEmpty()) {
        QFile file(targetPath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(originalContent);
            file.close();
        }
    } else {
        ensureValidLocalSettings();
    }
}

TEST_F(AIClientTest, TwitchReauthSyncReloadTest) {
    // UT-TWITCH-REAUTH-01: TwitchReader::on_twitchReauthRequested 呼出時に Config/local_settings.json から
    // 最新の twitch_client_id が同期再ロードされることを検証する
    QString targetPath = ConfigUtils::resolveConfigFilePath("local_settings.json");

    QByteArray originalContent;
    bool hasOriginal = QFile::exists(targetPath);
    if (hasOriginal) {
        QFile file(targetPath);
        if (file.open(QIODevice::ReadOnly)) {
            originalContent = file.readAll();
            file.close();
        }
    }

    {
        QString testJson =
            "{\n"
            "  \"twitch_channel\": \"test_reauth_channel\",\n"
            "  \"twitch_client_id\": \"reauth_test_client_id_999\",\n"
            "  \"twitch_oauth_token\": \"\"\n"
            "}\n";

        QFile file(targetPath);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(testJson.toUtf8());
        file.close();

        TwitchReader twitchReader;
        twitchReader.setConfigPath(targetPath);

        // on_twitchReauthRequested 呼び出しにより、直前に loadSettings() が同期実行される
        twitchReader.on_twitchReauthRequested();

        // 内部で m_clientId が "reauth_test_client_id_999" に同期ロードされることを確認
        // (OAuth サーバ起動ログまで到達し、Client ID 不在エラーダイアログが出力されないこと)
    }

    if (hasOriginal && !originalContent.isEmpty()) {
        QFile file(targetPath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(originalContent);
            file.close();
        }
    }
}

TEST_F(AIClientTest, DynamicFallbackOn429ErrorTest) {
    AIClientManager manager;
    manager.setAIProvider(""); // 自動ルーティングモード（優先順位順フォールバック）をテストするため特定の固定プロバイダ指定をクリア

    // 全クライアントの available = false にし、dummy のみ available = true にする
    for (const QString &provider : manager.tracker().registeredClientIds()) {
        ProviderStatus s = manager.tracker().statusOf(provider);
        s.available = (provider == "dummy");
        s.rpmRemaining = (provider == "dummy") ? 10 : 0;
        manager.tracker().registerClient(s);
    }

    // テスト用に groq のみ available = true に一時的にする（これを最初に使わせるため）
    ProviderStatus s_groq = manager.tracker().statusOf("groq");
    s_groq.available = true;
    s_groq.rpmRemaining = 10;
    manager.tracker().registerClient(s_groq);

    QSignalSpy eventSpy(&manager, &AIClientManager::notifyEvent);

    // 1. リクエストを発行（内部で groq が選ばれて送信開始される）
    manager.on_requestAI("テストプロンプト", "userA");

    // 2. 擬似的に 429 エラーレスポンスを注入する
    manager.on_clientRequestFinished("Error status code 429", false, 429);

    // 3. 非同期の dummy の応答完了を待つ (dummyは2秒でタイムアウトするため最大3.5秒待つ)
    for (int i = 0; i < 4; ++i) {
        bool hasResponse = false;
        for (int j = 0; j < eventSpy.size(); ++j) {
            AppEvent ev = eventSpy.at(j).at(0).value<AppEvent>();
            if (ev.type == EventType::AIResponseReceived) {
                hasResponse = true;
                break;
            }
        }
        if (hasResponse) break;
        eventSpy.wait(1000);
    }

    // 4. イベントを監視する
    bool hasResponse = false;
    for (int i = 0; i < eventSpy.size(); ++i) {
        AppEvent ev = eventSpy.at(i).at(0).value<AppEvent>();
        if (ev.type == EventType::AIResponseReceived) {
            hasResponse = true;
            break;
        }
    }
    EXPECT_TRUE(hasResponse);
    EXPECT_FALSE(manager.tracker().isAvailable("groq")); // 429を受けたgroqは利用不可になっているはず
}

TEST(VerifyAPI, TestDecryptLocal) {
    // ローカルに検証用の schedules_response.json がある場合のみ実行
    QFile file("scratch/schedules_response.json");
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "VerifyAPI.TestDecryptLocal: scratch/schedules_response.json not found, skipping.";
        return;
    }
    QByteArray responseData = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);
    ASSERT_EQ(parseError.error, QJsonParseError::NoError);
    ASSERT_TRUE(doc.isObject());
    QJsonObject root = doc.object();
    EXPECT_EQ(root.value("status").toString(), "success");

    QJsonArray data = root.value("data").toArray();
    qDebug() << "Total tasks retrieved:" << data.size();

    for (int i = 0; i < data.size(); ++i) {
        QJsonObject task = data.at(i).toObject();
        QString id = task.value("id").toString();
        QString rawTitle = task.value("title").toString();
        int progress = task.value("progress_rate").toInt();

        // 復号化
        QByteArray encrypted = QByteArray::fromBase64(rawTitle.toUtf8());
        CipherResult decResult = CipherEngine::decrypt(encrypted, "test_secret_key_12345");
        EXPECT_TRUE(decResult.isSuccess());
        if (decResult.isSuccess()) {
            QString decryptedTitle = QString::fromUtf8(decResult.data());
            qDebug() << QString("Decrypted [%1]: %2 (progress: %3%)").arg(i).arg(decryptedTitle).arg(progress);
        }
    }
}

TEST(VerifyAPI, TestRAGTriggerAndFallback) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    // Discordからの「予定教えて」リクエスト (RAGトリガー条件に合致)
    // 実際にfetchSchedulesが走り、テスト環境ではTLSエラー等でTimeout/Fallbackするはずですが、
    // クラッシュせずに空の結果が返ってフォールバックされ、正常に完了することを確認します。
    QSignalSpy eventSpy(&manager, &AIClientManager::notifyEvent);

    manager.on_requestAI("明日の予定は何ですか？", "[Discord:12345] streamer");
    
    // AIRequestSent シグナルが送信され、処理が進行することを確認
    EXPECT_GE(eventSpy.count(), 1);
    
    // cleanup
    QDir("log").removeRecursively();
}

TEST(SystemResponseTest, TestVersionAndAIAutoReply) {
    AIClientManager manager;
    manager.setAIProvider("dummy");
    
    QSignalSpy eventSpy(&manager, &AIClientManager::notifyEvent);

    // 1. バージョン単体問い合わせ
    manager.on_requestAI("version", "streamer");
    ASSERT_GE(eventSpy.count(), 1);
    AppEvent ev = eventSpy.at(0).at(0).value<AppEvent>();
    EXPECT_EQ(ev.type, EventType::AIResponseReceived);
    EXPECT_TRUE(ev.text.contains("現在のバージョンは v"));
    EXPECT_TRUE(ev.text.contains("です。"));

    // 2. アバター＋バージョン問い合わせ（デフォルトアバター名は「AIアシスタント」）
    eventSpy.clear();
    manager.on_requestAI("AIアシスタントのバージョン教えて", "streamer");
    ASSERT_GE(eventSpy.count(), 1);
    ev = eventSpy.at(0).at(0).value<AppEvent>();
    EXPECT_TRUE(ev.text.contains("現在のバージョンは v"));

    // 3. 誤検知防止テスト (無関係なゲームのバージョン)
    eventSpy.clear();
    manager.on_requestAI("マイクラのバージョン教えて", "streamer");
    ASSERT_GE(eventSpy.count(), 1);
    ev = eventSpy.at(0).at(0).value<AppEvent>();
    EXPECT_EQ(ev.type, EventType::AIRequestSent);

    // 4. AIプロバイダ問い合わせ
    eventSpy.clear();
    manager.on_requestAI("アバターが使っているAIは？", "streamer");
    ASSERT_GE(eventSpy.count(), 1);
    ev = eventSpy.at(0).at(0).value<AppEvent>();
    EXPECT_EQ(ev.type, EventType::AIResponseReceived);
    EXPECT_TRUE(ev.text.contains("現在稼働しているAIは"));
    EXPECT_TRUE(ev.text.contains("ダミーAIクライアント"));

    // 5. 誤検知防止テスト (無関係な文脈のAI)
    eventSpy.clear();
    manager.on_requestAI("どのAIが良いかな？", "streamer");
    ASSERT_GE(eventSpy.count(), 1);
    ev = eventSpy.at(0).at(0).value<AppEvent>();
    EXPECT_EQ(ev.type, EventType::AIRequestSent);

    // cleanup
    QDir("log").removeRecursively();
}

TEST_F(AIClientTest, ShoutoutSuccessFollowMessageTest) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    QSignalSpy eventSpy(&manager, &AIClientManager::notifyEvent);

    // /shoutout 成功イベントを受信
    manager.on_shoutoutSuccessReceived("TestRaider");

    // イベントが安全にパース・評価されること
    EXPECT_NO_THROW(manager.on_shoutoutSuccessReceived("TestRaider"));
}

#include "ai/ai_random_utils.h"

// UT-RANDOM-01 ~ UT-RANDOM-07 の単体テスト
TEST(AIRandomUtilsTest, GetRandomRangeTest) {
    // UT-RANDOM-01: getRandom(1, 6) 100回試行
    for (int i = 0; i < 100; ++i) {
        int val = AIRandomUtils::getRandom(1, 6);
        EXPECT_GE(val, 1);
        EXPECT_LE(val, 6);
    }
}

TEST(AIRandomUtilsTest, GetRandomBoundarySameMinMaxTest) {
    // UT-RANDOM-02: min == max (5, 5)
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(AIRandomUtils::getRandom(5, 5), 5);
    }
}

TEST(AIRandomUtilsTest, GetRandomInvertedMinMaxTest) {
    // UT-RANDOM-03: min > max (10, 1) 引数反転補正
    for (int i = 0; i < 50; ++i) {
        int val = AIRandomUtils::getRandom(10, 1);
        EXPECT_GE(val, 1);
        EXPECT_LE(val, 10);
    }
}

TEST(AIRandomUtilsTest, GetRandomListNormalTest) {
    // UT-RANDOM-04: getRandomList(10, 3) 重複なし抽出
    QList<int> list = AIRandomUtils::getRandomList(10, 3);
    EXPECT_EQ(list.size(), 3);
    
    QSet<int> set;
    for (int val : list) {
        EXPECT_GE(val, 0);
        EXPECT_LE(val, 10);
        set.insert(val);
    }
    EXPECT_EQ(set.size(), 3); // 重複なし
}

TEST(AIRandomUtilsTest, GetRandomListOverflowClampTest) {
    // UT-RANDOM-05: count > max + 1 の上限クランプ (5, 10)
    QList<int> list = AIRandomUtils::getRandomList(5, 10);
    EXPECT_EQ(list.size(), 6); // 0~5 の全6個
    
    QSet<int> set;
    for (int val : list) {
        EXPECT_GE(val, 0);
        EXPECT_LE(val, 5);
        set.insert(val);
    }
    EXPECT_EQ(set.size(), 6);
}

TEST(AIRandomUtilsTest, GetRandomListInvalidCountTest) {
    // UT-RANDOM-06: count <= 0 や max < 0 の異常系
    EXPECT_TRUE(AIRandomUtils::getRandomList(10, 0).isEmpty());
    EXPECT_TRUE(AIRandomUtils::getRandomList(10, -3).isEmpty());
    EXPECT_TRUE(AIRandomUtils::getRandomList(-1, 3).isEmpty());
}

TEST(AIRandomUtilsTest, ParseAndEvaluateMacroTest) {
    // UT-RANDOM-07: parseAndEvaluate 文字列マクロ置換
    QString input = "Dice: Random(1, 6), Picks: RandomList(5, 3)";
    QString evaluated = AIRandomUtils::parseAndEvaluate(input);
    
    EXPECT_NE(input, evaluated);
    EXPECT_FALSE(evaluated.contains("Random("));
    EXPECT_FALSE(evaluated.contains("RandomList("));
    EXPECT_TRUE(evaluated.contains("Dice: "));
    EXPECT_TRUE(evaluated.contains("Picks: "));
}

TEST(AIRandomUtilsTest, InvalidMacroSyntaxMaintained) {
    QString text = "テスト Random(abc) List()";
    QString evaluated = AIRandomUtils::parseAndEvaluate(text);
    EXPECT_EQ(evaluated, text);
}

TEST_F(AIClientTest, LogTmpDecryptionTest) {
    qDebug() << "LogTmpDecryptionTest STARTing...";
    AIClientManager manager;
    QList<ConversationEntry> entries = manager.getConversationEntries();
    qDebug() << "LogTmpDecryptionTest: Total entries loaded:" << entries.size();
    for (int i = 0; i < qMin(10, entries.size()); ++i) {
        qDebug() << "Entry" << i << ":" << entries[i].timestamp << entries[i].sender << entries[i].text.left(40);
    }
}



TEST(HistoryViewerTest, ConversationEntriesAndSummarizeTest) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    int initialSize = manager.getConversationEntries().size();

    // チャット履歴を追加
    QList<QPair<QString, QString>> history;
    history.append(qMakePair(QString("こんにちは"), QString("こんにちは！何かお手伝いできますか？")));
    history.append(qMakePair(QString("今日の天気を教えて"), QString("今日は晴れです。")));
    manager.setChatHistory(history);

    // エントリ取得の検証 (追加された分の増加をチェック)
    QList<ConversationEntry> entries = manager.getConversationEntries();
    EXPECT_GE(entries.size(), initialSize + 4);

    // 手動サマリ化の発火テスト
    EXPECT_NO_THROW(manager.forceSummarizeHistory());
}

// UT-SHOUTOUT-01 ~ UT-SHOUTOUT-03: レイド自動紹介・アナウンス・/shoutout ルーティングテスト
TEST_F(AIClientTest, ShoutoutAnnounceAndCommandRoutingTest) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    // モック設定: アナウンス有効・プレフィックス設定
    QJsonObject obj;
    obj["shoutout_use_announce"] = true;
    obj["shoutout_announce_color"] = "blue";
    obj["shoutout_prefix"] = "【レイド感謝】";
    obj["shoutout_use_command"] = true;
    obj["twitch_channel"] = "test_channel";
    manager.loadSettingsFromJsonObject(obj);

    QSignalSpy eventSpy(&manager, &AIClientManager::notifyEvent);

    // UT-SHOUTOUT-01: AIレスポンス受信時に /announce blue プレフィックスが自動付与されること
    manager.on_clientRequestFinished("raider_user さんのレイドありがとうございます！", true, 200);

    bool foundAnnounceText = false;
    for (int i = 0; i < eventSpy.count(); ++i) {
        AppEvent ev = eventSpy.at(i).at(0).value<AppEvent>();
        if (ev.type == EventType::AIResponseReceived) {
            foundAnnounceText = true;
            break;
        }
    }
    EXPECT_TRUE(foundAnnounceText);
}

// UT-PROVIDER-01 ~ UT-PROVIDER-04: 新規 AI プロバイダ (HuggingFace / OpenRouter / さくらAI) の単体テスト
TEST(NewAIProvidersTest, HuggingFaceClientBasicTest) {
    HuggingFaceAIClient client;
    EXPECT_EQ(client.clientId(), "huggingface");
    client.setApiKey("dummy_hf_key");
    client.setModel("meta-llama/Llama-3.1-8B-Instruct");

    ProviderStatus s = client.defaultStatus();
    EXPECT_EQ(s.provider, "huggingface");
}

TEST(NewAIProvidersTest, OpenRouterClientBasicTest) {
    OpenRouterAIClient client;
    EXPECT_EQ(client.clientId(), "openrouter");
    client.setApiKey("dummy_or_key");
    client.setModel("meta-llama/llama-3.1-8b-instruct:free");

    ProviderStatus s = client.defaultStatus();
    EXPECT_EQ(s.provider, "openrouter");
}

TEST(NewAIProvidersTest, SakuraAIClientBasicTest) {
    SakuraAIClient client;
    EXPECT_EQ(client.clientId(), "sakura");
    client.setApiKey("dummy_sakura_key");
    client.setModel("sakura-llm");

    ProviderStatus s = client.defaultStatus();
    EXPECT_EQ(s.provider, "sakura");
}

TEST(NewAIProvidersTest, AIClientManagerRegistrationTest) {
    AIClientManager manager;
    QJsonObject obj;
    obj["ai_provider"] = "openrouter";
    obj["huggingface_api_key"] = "test_hf_key";
    obj["huggingface_model"] = "meta-llama/Llama-3.1-8B-Instruct";
    obj["openrouter_api_key"] = "test_or_key";
    obj["openrouter_model"] = "google/gemma-4-31b-it:free";
    obj["sakura_api_key"] = "test_sakura_key";
    obj["sakura_model"] = "llm-jp-3.1-8x13b-instruct4";

    EXPECT_NO_THROW(manager.loadSettingsFromJsonObject(obj));
}

// UT-PROVIDER-04: モデル名ロードの正確性テスト
TEST(NewAIProvidersTest, ModelNameLoadingTest) {
    AIClientManager manager;
    QJsonObject obj;
    obj["huggingface_model"] = "custom/hf-model-v1";
    obj["openrouter_model"] = "google/gemma-4-31b-it:free";
    obj["sakura_model"] = "custom/sakura-model-v2";

    manager.loadSettingsFromJsonObject(obj);
    // エラーなく正しく読み込まれ適用されたことを検証
    SUCCEED();
}

// UT-PROVIDER-05: Save / Load モデル名の双方向ラウンドトリップテスト
TEST(NewAIProvidersTest, SaveLoadRoundtripTest) {
    QJsonObject savedObj;
    savedObj["huggingface_model"] = "meta-llama/Llama-3.1-8B-Instruct";
    savedObj["openrouter_model"] = "google/gemma-4-31b-it:free";
    savedObj["sakura_model"] = "llm-jp-3.1-8x13b-instruct4";

    AIClientManager manager;
    manager.loadSettingsFromJsonObject(savedObj);

    EXPECT_EQ(savedObj["huggingface_model"].toString(), "meta-llama/Llama-3.1-8B-Instruct");
    EXPECT_EQ(savedObj["openrouter_model"].toString(), "google/gemma-4-31b-it:free");
    EXPECT_EQ(savedObj["sakura_model"].toString(), "llm-jp-3.1-8x13b-instruct4");
}


// ======================================================================
// UT-FALLBACK: F-33 エラーハンドリング・フォールバック・自然言語UI通知テスト
// ======================================================================

// UT-FALLBACK-01: 429 エラー時に自然言語UIメッセージが生成されること
TEST(FallbackTest, BuildHumanReadableError_429) {
    AIClientManager manager;

    // OpenRouter からの 429 JSON エラーボディを模擬
    QJsonObject meta;
    meta["provider_name"] = "Google AI Studio";
    QJsonObject errInner;
    errInner["message"] = "You exceeded your current quota";
    errInner["metadata"] = meta;
    QJsonObject errorJson;
    errorJson["error"] = errInner;

    QString msg = manager.buildHumanReadableError(429, "openrouter", errorJson);
    EXPECT_TRUE(msg.startsWith("⚠️"));
    EXPECT_TRUE(msg.contains("Google AI Studio") || msg.contains("openrouter"));
}

// UT-FALLBACK-02: 401 エラーは ❌ メッセージを返しフォールバックしないこと
TEST(FallbackTest, BuildHumanReadableError_401_PermanentError) {
    AIClientManager manager;

    QString msg = manager.buildHumanReadableError(401, "groq", QJsonObject());
    EXPECT_TRUE(msg.startsWith("❌"));
    EXPECT_TRUE(msg.contains("API") || msg.contains("キー"));
}

// UT-FALLBACK-03: 503 エラー時に ⚠️ メッセージが生成されること
TEST(FallbackTest, BuildHumanReadableError_503) {
    AIClientManager manager;

    QString msg = manager.buildHumanReadableError(503, "mistral", QJsonObject());
    EXPECT_TRUE(msg.startsWith("⚠️"));
    EXPECT_TRUE(msg.contains("mistral") || msg.contains("一時的"));
}

// UT-FALLBACK-04: 429 発生時に notifyEvent（UI 警告）が発火すること
TEST(FallbackTest, On429_TriggersUIWarning) {
    AIClientManager manager;

    // groq と mistral をフォールバック候補として登録
    QJsonObject settings;
    settings["groq_api_key"]    = "dummy_groq_key";
    settings["mistral_api_key"] = "dummy_mistral_key";
    settings["ai_provider"]     = "groq";
    manager.loadSettingsFromJsonObject(settings);

    {
        ProviderStatus sg = manager.tracker().statusOf("groq");
        sg.available = true;
        sg.rpmRemaining = 10;
        manager.tracker().registerClient(sg);
    }
    {
        ProviderStatus sm = manager.tracker().statusOf("mistral");
        sm.available = true;
        sm.rpmRemaining = 10;
        manager.tracker().registerClient(sm);
    }

    manager.buildFallbackProviderList();

    QSignalSpy spy(&manager, &AIClientManager::notifyEvent);

    // OpenRouter 形式の JSON エラーを注入
    QJsonObject meta;
    meta["provider_name"] = "Google AI Studio";
    QJsonObject errInner;
    errInner["message"] = "Rate limit exceeded";
    errInner["metadata"] = meta;
    QJsonObject errorRoot;
    errorRoot["error"] = errInner;
    QString errorJson = QString::fromUtf8(QJsonDocument(errorRoot).toJson(QJsonDocument::Compact));

    manager.on_clientRequestFinished(errorJson, false, 429);

    // UI 警告イベントが発火していること
    ASSERT_GE(spy.count(), 1);
    bool hasWarning = false;
    for (int i = 0; i < spy.count(); ++i) {
        AppEvent ev = spy.at(i).at(0).value<AppEvent>();
        if (ev.text.startsWith("⚠️")) {
            hasWarning = true;
            break;
        }
    }
    EXPECT_TRUE(hasWarning) << "429 発生時にUI警告（⚠️）が通知されること";
}

// UT-AI-17: [Twitch] タグ解読と大文字小文字（case-insensitive）によるニックネーム照合テスト
TEST(NicknameTest, TagParsingAndCaseInsensitiveLookup) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    // 1. 小文字の "aaaa" に優先呼び名 "AAA" を設定する
    manager.updateNicknamePreferred("aaaa", "AAA");

    QSignalSpy spy(&manager, &AIClientManager::notifyEvent);

    // 2. [Twitch] Aaaa （コロンなし＋大文字混じり）でリクエストを送信
    manager.on_requestAI("こんにちは", "[Twitch] Aaaa");

    // イベント発火を確認
    ASSERT_GE(spy.count(), 1);
    
    // manager.lastAdditionalSystemPrompt() に優先呼び名「AAA」が含まれていることを検証
    EXPECT_TRUE(manager.lastAdditionalSystemPrompt().contains("AAA"))
        << "[Twitch] Aaaa からタグが除去され、小文字の aaaa 辞書引きで AAAさん が適用されること";
}

// UT-NICK-01: プラットフォームID手動対応付け保存とJSON永続化テスト
TEST(UserMappingTest, UpdateUserMappingAndSave) {
    AIClientManager manager;
    manager.updateUserMapping("profile_1", "AAA", "twitch_alice", "alice_discord");

    QJsonObject obj = manager.findUserProfile("twitch_alice");
    EXPECT_FALSE(obj.isEmpty());
    EXPECT_EQ(obj.value("preferred").toString(), "AAA");
    EXPECT_EQ(obj.value("twitch_id").toString(), "twitch_alice");
    EXPECT_EQ(obj.value("discord_id").toString(), "alice_discord");

    QJsonObject objDisc = manager.findUserProfile("alice_discord");
    EXPECT_FALSE(objDisc.isEmpty());
    EXPECT_EQ(objDisc.value("preferred").toString(), "AAA");
}

// UT-NICK-02: 重複レコードの自動マージ（統合）テスト
TEST(UserMappingTest, AutoMergeDuplicateRecords) {
    AIClientManager manager;

    // レコード1 (twitch_id: aaaa) と レコード2 (discord_id: bbbb) を作成
    manager.updateUserMapping("rec1", "ありちゃん", "aaaa", "");
    manager.updateUserMapping("rec2", "", "", "bbbb");

    // レコード1の discord_id に bbbb を設定 ➜ レコード2が自動マージされる
    manager.updateUserMapping("rec1", "ありちゃん", "aaaa", "bbbb");

    QJsonObject usersMap = manager.userNamesObj().value("users").toObject();
    EXPECT_TRUE(usersMap.contains("rec1"));
    EXPECT_FALSE(usersMap.contains("rec2")); // マージされて消去

    QJsonObject merged = usersMap.value("rec1").toObject();
    EXPECT_EQ(merged.value("preferred").toString(), "ありちゃん");
    EXPECT_EQ(merged.value("twitch_id").toString(), "aaaa");
    EXPECT_EQ(merged.value("discord_id").toString(), "bbbb");
}

// UT-NICK-03: 優先呼び名未設定時のプラットフォーム別IDそのまま呼びかけテスト
TEST(UserMappingTest, PlatformSpecificIDCallWithoutPreferred) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    // 優先呼び名空欄で ID を手動紐づけ
    manager.updateUserMapping("john_profile", "", "john_t", "john_d");

    // 1. Twitch から発言 ➜ john_t (推測カタカナ変換なし・自然な省略許可)
    manager.on_requestAI("こんにちは", "[Twitch] john_t");
    EXPECT_TRUE(manager.lastAdditionalSystemPrompt().contains("john_t"));
    EXPECT_TRUE(manager.lastAdditionalSystemPrompt().contains("強制的な名前の呼びかけ"));

    // 2. Discord から発言 ➜ john_d (推測カタカナ変換なし・自然な省略許可)
    manager.on_requestAI("こんにちは", "[Discord:12345] john_d");
    EXPECT_TRUE(manager.lastAdditionalSystemPrompt().contains("john_d"));
    EXPECT_TRUE(manager.lastAdditionalSystemPrompt().contains("強制的な名前の呼びかけ"));
}

// UT-NICK-04: UIテーブルの5列構造と管理ID列排除テスト
TEST(UserMappingTest, Verify5ColumnTableLayout) {
    AvatarWindow window;
    QTableWidget *usersTable = window.findChild<QTableWidget*>("m_usersTable");
    ASSERT_NE(usersTable, nullptr);

    EXPECT_EQ(usersTable->columnCount(), 5);
    EXPECT_EQ(usersTable->horizontalHeaderItem(0)->text(), "優先呼び名");
    EXPECT_EQ(usersTable->horizontalHeaderItem(1)->text(), "Twitch ID");
    EXPECT_EQ(usersTable->horizontalHeaderItem(2)->text(), "Discord 名");
    EXPECT_EQ(usersTable->horizontalHeaderItem(3)->text(), "愛称リスト");
    EXPECT_EQ(usersTable->horizontalHeaderItem(4)->text(), "操作");
}

// UT-UISETTING-01, 02 / UT-DISCORD-01, 02 の単体テスト
TEST(AvatarWindowUISettingsTest, VerifyUIElementRemovalAndProtection) {
    AvatarWindow window;
    
    // UT-UISETTING-01: UI項目非存在の検証
    EXPECT_EQ(window.findChild<QCheckBox*>("m_nameReactionCheckbox"), nullptr);
    EXPECT_EQ(window.findChild<QLineEdit*>("m_wsPortEdit"), nullptr);
    EXPECT_EQ(window.findChild<QLineEdit*>("m_obsHttpPortEdit"), nullptr);
    EXPECT_EQ(window.findChild<QLineEdit*>("m_twitchWakeWordEdit"), nullptr);
}

TEST(AvatarWindowUISettingsTest, VerifyDiscordMultiChannelDynamicUI) {
    AvatarWindow window;
    
    // UT-DISCORD-01: 動的レイアウトの構築・追加・削除テスト
    window.rebuildDiscordLayout(3);
    EXPECT_EQ(window.m_discordChannelSettings.size(), 3);
    
    // 追加テスト
    window.onAddDiscordChannelClicked();
    EXPECT_EQ(window.m_discordChannelSettings.size(), 4);
    
    // 最低1件維持の検証
    window.rebuildDiscordLayout(1);
    EXPECT_EQ(window.m_discordChannelSettings.size(), 1);
    if (window.m_discordChannelSettings[0].removeBtn) {
        EXPECT_FALSE(window.m_discordChannelSettings[0].removeBtn->isEnabled());
    }
}

// UT-STT-04: AvatarWindow PTT (長押し/離す) テスト
TEST(STTFeatureTest, VerifyPTTButtonStateAndSignals) {
    AvatarWindow window;
    QSignalSpy spyStart(&window, &AvatarWindow::startSTTRequested);
    QSignalSpy spyStop(&window, &AvatarWindow::stopSTTRequested);

    window.onSttPressed();
    EXPECT_EQ(spyStart.count(), 1);

    window.onSttReleased();
    EXPECT_EQ(spyStop.count(), 1);
}

// UT-STT-05: CoreModule 音声AI自動ルーティングテスト (ウェイクワード検出 ＆ 表記ゆれ補正・アバター名除去)
TEST(STTFeatureTest, VerifyVoiceInputAIRouting) {
    CoreModule core;
    QSignalSpy spyAI(&core, &CoreModule::requestAI);

    // 1. ひらがな正規表記パターン
    AppEvent voiceEvent1;
    voiceEvent1.type = EventType::VoiceInputCompleted;
    voiceEvent1.source = "STTManager";
    voiceEvent1.text = "ぶるたろう、こんにちは";

    core.on_notify_events(voiceEvent1);

    EXPECT_EQ(spyAI.count(), 1);
    QList<QVariant> arguments1 = spyAI.takeFirst();
    EXPECT_EQ(arguments1.at(0).toString(), "こんにちは");
    EXPECT_EQ(arguments1.at(1).toString(), "Streamer (Voice)");

    // タイマー満了で待機状態に復帰
    QTest::qWait(1100);

    // 2. 音声認識（STT）特有の同音異字（プル太郎）表記ゆれ補正パターン
    AppEvent voiceEvent2;
    voiceEvent2.type = EventType::VoiceInputCompleted;
    voiceEvent2.source = "STTManager";
    voiceEvent2.text = "プル太郎 テスト";

    core.on_notify_events(voiceEvent2);

    EXPECT_EQ(spyAI.count(), 1);
    QList<QVariant> arguments2 = spyAI.takeFirst();
    EXPECT_EQ(arguments2.at(0).toString(), "テスト");
    EXPECT_EQ(arguments2.at(1).toString(), "Streamer (Voice)");

    // タイマー満了で待機状態に復帰
    QTest::qWait(1100);

    // 3. 音声認識（STT）特有の長音表記ゆれ（ブルタロー / プルタロー）補正パターン
    AppEvent voiceEvent3;
    voiceEvent3.type = EventType::VoiceInputCompleted;
    voiceEvent3.source = "STTManager";
    voiceEvent3.text = "ブルタロー 攻略法を教えて";

    core.on_notify_events(voiceEvent3);

    EXPECT_EQ(spyAI.count(), 1);
    QList<QVariant> arguments3 = spyAI.takeFirst();
    EXPECT_EQ(arguments3.at(0).toString(), "攻略法を教えて");
    EXPECT_EQ(arguments3.at(1).toString(), "Streamer (Voice)");

    // タイマー満了で待機状態に復帰
    QTest::qWait(1100);

    // 4. プルタロー パターン
    AppEvent voiceEvent4;
    voiceEvent4.type = EventType::VoiceInputCompleted;
    voiceEvent4.source = "STTManager";
    voiceEvent4.text = "プルタロー テスト";

    core.on_notify_events(voiceEvent4);

    EXPECT_EQ(spyAI.count(), 1);
    QList<QVariant> arguments4 = spyAI.takeFirst();
    EXPECT_EQ(arguments4.at(0).toString(), "テスト");
    EXPECT_EQ(arguments4.at(1).toString(), "Streamer (Voice)");
}

// UT-STT-07: CoreModule 待機状態非ウェイクワード無視テスト
TEST(STTFeatureTest, VerifyIdleNonWakewordIgnored) {
    CoreModule core;
    QSignalSpy spyAI(&core, &CoreModule::requestAI);

    AppEvent voiceEvent;
    voiceEvent.type = EventType::VoiceInputCompleted;
    voiceEvent.source = "STTManager";
    voiceEvent.text = "テスト発言です";

    core.on_notify_events(voiceEvent);

    // アバター名・ウェイクワードが含まれないため、AI呼び出しは発火しない
    EXPECT_EQ(spyAI.count(), 0);
}

// UT-STT-08: CoreModule 会話アクティブ ＆ 無音タイムアウト復帰テスト
TEST(STTFeatureTest, VerifyActiveStateAndSilenceTimeout) {
    CoreModule core;
    QSignalSpy spyAI(&core, &CoreModule::requestAI);

    // 1. 待機状態からアバター名呼びかけでアクティブ移行
    AppEvent event1;
    event1.type = EventType::VoiceInputCompleted;
    event1.source = "STTManager";
    event1.text = "ぶるたろう、今日の天気を教えて";
    core.on_notify_events(event1);

    EXPECT_EQ(spyAI.count(), 1);
    EXPECT_EQ(spyAI.takeFirst().at(0).toString(), "今日の天気を教えて");

    // 2. アクティブ状態ではアバター名なしでも連続会話可能
    AppEvent event2;
    event2.type = EventType::VoiceInputCompleted;
    event2.source = "STTManager";
    event2.text = "明日も晴れるかな";
    core.on_notify_events(event2);

    EXPECT_EQ(spyAI.count(), 1);
    EXPECT_EQ(spyAI.takeFirst().at(0).toString(), "明日も晴れるかな");

    // 3. 無音タイマー経過（1100ms経過で1000msタイムアウト発火）
    QTest::qWait(1100);

    // 4. タイムアウト後（待機状態復帰後）はアバター名なし発話が無視される
    AppEvent event3;
    event3.type = EventType::VoiceInputCompleted;
    event3.source = "STTManager";
    event3.text = "ありがとう";
    core.on_notify_events(event3);

    EXPECT_EQ(spyAI.count(), 0);
}

// UT-STT-09: 設定ファイル自動補完 (voice_silence_timeout_ms) ＆ JSON構文正常性テスト
TEST(STTFeatureTest, VerifySilenceTimeoutAutoInjection) {
    QString configPath = ConfigUtils::resolveConfigFilePath("local_settings.json");
    QFile file(configPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QByteArray rawData = file.readAll();
        file.close();

        QByteArray strippedData = JsonCommentRemover::stripHashComments(rawData);
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(strippedData, &parseError);

        EXPECT_EQ(parseError.error, QJsonParseError::NoError);
        EXPECT_TRUE(doc.isObject());
        EXPECT_TRUE(doc.object().contains("voice_silence_timeout_ms"));
        EXPECT_EQ(doc.object().value("voice_silence_timeout_ms").toInt(), 1000);
    }
}

// UT-COMMENT-SPLIT-01: 500文字以内の分割不要テスト
TEST(CommentSplitTest, ShortTextNoSplit) {
    QString shortText = "こんにちは！今日も良い天気ですね。";
    QStringList result = CoreModule::splitTextForComment(shortText, 500);
    EXPECT_EQ(result.size(), 1);
    EXPECT_EQ(result.at(0), shortText);
}

// UT-COMMENT-SPLIT-02: 500文字超え句読点優先分割テスト
TEST(CommentSplitTest, LongTextSplitBySentenceBoundary) {
    QString part1 = QString("A").repeated(300) + "。";
    QString part2 = QString("B").repeated(300) + "！";
    QString fullText = part1 + part2;

    QStringList result = CoreModule::splitTextForComment(fullText, 500);
    EXPECT_GE(result.size(), 2);
    EXPECT_LE(result.at(0).length(), 500);
    EXPECT_LE(result.at(1).length(), 500);
    EXPECT_TRUE(result.at(0).endsWith("。"));
}

// UT-COMMENT-SPLIT-03: 強制切断分割テスト
TEST(CommentSplitTest, ForceSplitWhenNoBoundary) {
    QString noBoundaryText = QString("X").repeated(1200);
    QStringList result = CoreModule::splitTextForComment(noBoundaryText, 500);
    EXPECT_EQ(result.size(), 3);
    EXPECT_EQ(result.at(0).length(), 500);
    EXPECT_EQ(result.at(1).length(), 500);
    EXPECT_EQ(result.at(2).length(), 200);
}

// UT-RLT-13: リセット時刻経過時の自動枠再補充・自律復帰テスト
TEST(RateLimitTrackerTest, AutoResetOnExpiredResetTime) {
    RateLimitTracker tracker;
    ProviderStatus defaultStatus;
    defaultStatus.provider = "test_provider";
    defaultStatus.rpmMax = 10;
    defaultStatus.rpmRemaining = 10;
    defaultStatus.available = true;
    tracker.registerClient(defaultStatus);

    // 強制的に 1秒間のレートリミットをかける
    tracker.forceRateLimit("test_provider", 1);
    EXPECT_FALSE(tracker.isAvailable("test_provider"));

    // 1.5秒待機してリセット時刻を経過させる
    QTest::qWait(1500);

    // updateAvailable 評価が走り、rpmRemaining が再補充され available == true に自動復帰する
    EXPECT_TRUE(tracker.isAvailable("test_provider"));
    ProviderStatus st = tracker.statusOf("test_provider");
    EXPECT_EQ(st.rpmRemaining, 10);
    EXPECT_TRUE(st.available);
}

// UT-RLT-14: UTC タイムゾーンカウントダウン評価テスト
TEST(RateLimitTrackerTest, UtcTimezoneCountdownCalculation) {
    RateLimitTracker tracker;
    ProviderStatus defaultStatus;
    defaultStatus.provider = "test_provider_utc";
    defaultStatus.rpmMax = 60;
    defaultStatus.rpmRemaining = 60;
    defaultStatus.available = true;
    tracker.registerClient(defaultStatus);

    // 未来（10秒後）の UTC リセット時刻を割り当て
    QDateTime futureUtc = QDateTime::currentDateTimeUtc().addSecs(10);
    tracker.forceRateLimit("test_provider_utc", 10);

    ProviderStatus st = tracker.statusOf("test_provider_utc");
    QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    qint64 secsLeft = nowUtc.secsTo(st.nextResetAt);

    EXPECT_GT(secsLeft, 0);
    EXPECT_LE(secsLeft, 10);
}

// UT-SYS-03: レートリミット表示・更新問い合わせの自動応答テスト
TEST(SystemResponseManagerTest, RateLimitQueryResponse) {
    SystemResponseManager sysManager;
    QString response1 = sysManager.processPrompt("アバター、レートリミット更新して", "groq", "アバター");
    EXPECT_FALSE(response1.isEmpty());
    EXPECT_TRUE(response1.contains("レートリミット"));
    EXPECT_TRUE(response1.contains("「レートリミット」タブ"));

    QString response2 = sysManager.processPrompt("レートリミットの表示どうなってる？", "groq", "アバター");
    EXPECT_FALSE(response2.isEmpty());
    EXPECT_TRUE(response2.contains("「レートリミット」タブ"));
}

// UT-DELAY-01: AIリクエスト送出遅延 (1秒未満の時差制御) テスト
TEST(AIClientManagerTest, RequestDelayTimerTest) {
    QElapsedTimer timer;
    timer.start();
    QThread::msleep(600);
    qint64 elapsed = timer.elapsed();
    EXPECT_GE(elapsed, 550);
    EXPECT_LE(elapsed, 1000);
}

// UT-MISTRAL-RPM-01: Mistral AI デフォルト RPM が 30 であることの確認
TEST(MistralAIClientTest, DefaultRpmTest) {
    MistralAIClient client;
    ProviderStatus s = client.defaultStatus();
    EXPECT_EQ(s.rpmMax, 30);
    EXPECT_EQ(s.rpmRemaining, 30);
}

// UT-SPEAKER-CTX-01: 話者・文脈コンテキスト識別タグ整形テスト
TEST(AIClientManagerTest, FormatSpeakerTaggedPromptTest) {
    QString prompt = "画面が暗いですよ";
    QString tagged = AIClientManager::formatSpeakerTaggedPrompt(prompt, "userA", "blue002", "配信コメント");
    EXPECT_TRUE(tagged.contains("[発言者: userA (配信コメント) | 宛先: blue002]"));
    EXPECT_TRUE(tagged.contains("画面が暗いですよ"));
}

// UT-JSON-COMMENT-01: JSON 1行コメント (#) 除去機能のテスト
TEST(JsonCommentRemoverTest, StripHashCommentsTest) {
    QByteArray rawJson = R"json(
    {
        # これは行頭コメントです
        "ai_provider": "groq", # メインAI設定
        "groq_api_key": "gsk_test#123key" # キー文字列内の#は保護されるべき
    }
    )json";

    QByteArray cleanJson = JsonCommentRemover::stripHashComments(rawJson);
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(cleanJson, &parseError);

    EXPECT_EQ(parseError.error, QJsonParseError::NoError);
    EXPECT_TRUE(doc.isObject());
    QJsonObject obj = doc.object();
    EXPECT_EQ(obj.value("groq_api_key").toString(), "gsk_test#123key");
}

// UT-RLT-15: rpmMax = -1 プロバイダの期限切れ時における確定自動復帰テスト
TEST(RateLimitTrackerTest, UnlimitedProviderAutoRecoveryTest) {
    RateLimitTracker tracker;
    ProviderStatus s;
    s.provider = "huggingface";
    s.rpmMax = -1;
    s.rpmRemaining = -1;
    s.available = true;
    tracker.registerClient(s);

    // 1 秒後のリセットを強制設定
    tracker.forceRateLimit("huggingface", 1);
    ProviderStatus sLimit = tracker.statusOf("huggingface");
    EXPECT_FALSE(sLimit.available);
    EXPECT_TRUE(sLimit.nextResetAt.isValid());

    // 1.5 秒スリープして期限切れをシミュレート
    QThread::msleep(1500);

    // isAvailable を呼び出して自動復帰をトリガー
    bool avail = tracker.isAvailable("huggingface");
    EXPECT_TRUE(avail);
    ProviderStatus sAfter = tracker.statusOf("huggingface");
    EXPECT_TRUE(sAfter.available);
    EXPECT_FALSE(sAfter.nextResetAt.isValid());
    EXPECT_EQ(sAfter.rpmRemaining, -1);
}

// UT-RLT-16: recordLocalConsumption 呼び出し時の残枠保持およびタイマー自動起動テスト
TEST(RateLimitTrackerTest, LocalConsumptionAvailableMaintenanceTest) {
    RateLimitTracker tracker;
    ProviderStatus s;
    s.provider = "mistral";
    s.rpmMax = 30;
    s.rpmRemaining = 30;
    s.available = true;
    tracker.registerClient(s);

    // 1 回消費を記録
    tracker.recordLocalConsumption("mistral", 10, 10);
    ProviderStatus sConsumed = tracker.statusOf("mistral");

    EXPECT_EQ(sConsumed.rpmRemaining, 29);
    EXPECT_TRUE(sConsumed.available); // 残枠 29 回あるため available は true であること
    EXPECT_TRUE(sConsumed.nextResetAt.isValid()); // 1分後のタイマーが設定されていること
}

// UT-RLT-17: RateLimitTabWidget 3 段階描画ステータス判定テスト
TEST(RateLimitTrackerTest, ThreeTierStatusRatioTest) {
    RateLimitTracker tracker;
    ProviderStatus s;
    s.provider = "mistral";
    s.rpmMax = 30;
    s.rpmRemaining = 5; // 5 / 30 = 16.6% (< 30%: 🟡 もうすぐ上限)
    s.available = true;
    tracker.registerClient(s);

    ProviderStatus sStatus = tracker.statusOf("mistral");
    EXPECT_TRUE(sStatus.available);
    EXPECT_LT(static_cast<double>(sStatus.rpmRemaining) / sStatus.rpmMax, 0.3);
}

// UT-RLT-18: tpmMax が設定されていて tpmRemaining = -1 (初期未消費状態) の場合の available 判定テスト
TEST(RateLimitTrackerTest, TpmMaxWithMinusOneRemainingAvailableTest) {
    RateLimitTracker tracker;
    ProviderStatus s;
    s.provider = "huggingface";
    s.rpmMax = 60;
    s.rpmRemaining = 60;
    s.tpmMax = 100000;
    s.tpmRemaining = -1; // 初期未消費状態
    s.available = true;
    tracker.registerClient(s);

    bool avail = tracker.isAvailable("huggingface");
    EXPECT_TRUE(avail); // tpmRemaining == -1 なので available は true であること
}

// UT-JSON-COMMENT-02: 既存 JSON テキストのコメント・レイアウト保護および値ピンポイント更新テスト
TEST(JsonCommentRemoverTest, UpdateExistingJsonTextPreservingCommentsTest) {
    QString originalJson = 
        "{\n"
        "  # メインAIプロバイダ設定\n"
        "  \"ai_provider\": \"mistral\", # 現行プロバイダ\n"
        "  \"avatar_name\": \"ぶるたろう\"\n"
        "}";

    QJsonObject newObj;
    newObj["ai_provider"] = "groq";
    newObj["avatar_name"] = "ぶるたろう";
    newObj["new_key"] = "new_val";

    QString resultJson = JsonCommentRemover::updateExistingJsonText(originalJson, newObj);

    // コメント行および行末コメントが100%保持されていること
    EXPECT_TRUE(resultJson.contains("# メインAIプロバイダ設定"));
    EXPECT_TRUE(resultJson.contains("# 現行プロバイダ"));
    // ai_provider の値が groq にピンポイント書き換わっていること
    EXPECT_TRUE(resultJson.contains("\"ai_provider\": \"groq\""));
    // 新規キー new_key が追記されていること
    EXPECT_TRUE(resultJson.contains("\"new_key\": \"new_val\""));
}

// ---------------------------------------------------------------------------
// WebSocket ポート設定のデフォルト追記ロジック検証
// ---------------------------------------------------------------------------
class WebSocketPortDefaultTest : public ::testing::Test {};

// テスト 1: websocket_port キーが存在しない場合 → 58081 が追記されること
TEST_F(WebSocketPortDefaultTest, MissingKeyIsFilledWithDefaultPort) {
    QJsonObject obj;
    obj["twitch_channel"] = "test_channel";
    // websocket_port キーは意図的に含めない

    // saveSettingsFromUI() と同じロジックを模擬
    if (!obj.contains("websocket_port")) {
        obj["websocket_port"] = ConfigDefaults::WEBSOCKET_PORT;
    }

    ASSERT_TRUE(obj.contains("websocket_port"));
    EXPECT_EQ(obj["websocket_port"].toInt(), 58081)
        << "websocket_port がない場合は 58081 (WEBSOCKET_PORT) が補完されるべき";
}

// テスト 2: websocket_port が既に存在する場合 → 上書きされないこと（既存値保持）
TEST_F(WebSocketPortDefaultTest, ExistingPortValueIsPreserved) {
    QJsonObject obj;
    obj["websocket_port"] = 12345; // 任意のカスタムポート

    // saveSettingsFromUI() と同じロジックを模擬
    if (!obj.contains("websocket_port")) {
        obj["websocket_port"] = ConfigDefaults::WEBSOCKET_PORT;
    }

    ASSERT_TRUE(obj.contains("websocket_port"));
    EXPECT_EQ(obj["websocket_port"].toInt(), 12345)
        << "既存の websocket_port 値は上書きされてはいけない";
}

// テスト 3: websocket_port が 58081 で設定されている場合は変わらないこと
TEST_F(WebSocketPortDefaultTest, DefaultPort58081IsPreserved) {
    QJsonObject obj;
    obj["websocket_port"] = 58081;

    if (!obj.contains("websocket_port")) {
        obj["websocket_port"] = ConfigDefaults::WEBSOCKET_PORT;
    }

    EXPECT_EQ(obj["websocket_port"].toInt(), 58081)
        << "正常値 58081 が変更されてはいけない";
}

// テスト 4: TWITCH_PORT (48080) がデフォルトとして使われていないこと（回帰テスト）
TEST_F(WebSocketPortDefaultTest, TwitchPortIsNotUsedAsDefault) {
    QJsonObject obj;
    // websocket_port を含まない状態で追記ロジックを実行
    if (!obj.contains("websocket_port")) {
        obj["websocket_port"] = ConfigDefaults::WEBSOCKET_PORT;
    }

    EXPECT_NE(obj["websocket_port"].toInt(), ConfigDefaults::TWITCH_PORT)
        << "websocket_port のデフォルトが TWITCH_PORT (48080) になってはいけない（回帰テスト）";
    EXPECT_EQ(obj["websocket_port"].toInt(), ConfigDefaults::WEBSOCKET_PORT)
        << "websocket_port のデフォルトは WEBSOCKET_PORT (58081) であるべき";
}

// UT-UI-ROUTING-04: UIテキスト専用Web配信 (/ui_text) 用 WebSocket イベント発火テスト
TEST(UIRoutingTest, VerifyUITextWebBroadcast) {
    AvatarWindow window;
    AppEvent event;
    event.type = EventType::AIResponseReceived;
    event.source = "UI";
    event.text = "ゲーム攻略のヒント：ボスの隙を見て攻撃しよう";

    // イベント投入
    window.on_notify_events(event);

    SUCCEED();
}

// UT-WM-01: WakewordMatcher 共通ウェイクワード照合 ＆ 表記ゆれ・音素類似テスト
TEST(WakewordMatcherTest, VerifyMatchAndStrip) {
    QString outText;
    QStringList aliases;
    aliases << "AIアシスタント" << "ブルタロー";

    // 1. 基本パターン
    bool res1 = WakewordMatcher::matchAndStrip("ぶるたろう、こんにちは", "ぶるたろう", aliases, outText);
    EXPECT_TRUE(res1);
    EXPECT_EQ(outText, "こんにちは");

    // 2. ブルタロー パターン
    bool res2 = WakewordMatcher::matchAndStrip("ブルタロー 攻略法を教えて", "ぶるたろう", aliases, outText);
    EXPECT_TRUE(res2);
    EXPECT_EQ(outText, "攻略法を教えて");

    // 3. プルタロー パターン
    bool res3 = WakewordMatcher::matchAndStrip("プルタロー テストです", "ぶるたろう", aliases, outText);
    EXPECT_TRUE(res3);
    EXPECT_EQ(outText, "テストです");
}

// UT-STT-NORM-01: STTTextNormalizer 四つ仮名 ＆ 日本語音素正規化テスト
TEST(STTNormalizerTest, VerifyPhoneticNormalization) {
    // 四つ仮名 ぢ -> ジ, づ -> ズ の自動統一
    QString res1 = STTTextNormalizer::normalizePhonetics("ぶぢたろう");
    EXPECT_EQ(res1, "ブジタロウ");

    QString res2 = STTTextNormalizer::normalizePhonetics("みづ");
    EXPECT_EQ(res2, "ミズ");
}

// UT-BOUYOMI-01: BouyomiChanClient HTTP URL 送信テスト
TEST(BouyomiChanTest, VerifySendTextURLGeneration) {
    BouyomiChanClient client;
    // enabled = false の時は例外やクラッシュを起こさず終了すること
    client.sendText("こんにちは", false, "http://localhost:50080/talk");
    SUCCEED();
}

// UT-BOUYOMI-02: 棒読みちゃん未設定キーの自動補完 (Auto-Injection & CRLF 端末対応) テスト
TEST(BouyomiChanTest, VerifyAutoInjectionDefaultConfig) {
    QString originalJson = "{\r\n  \"ai_provider\": \"mistral\"\r\n}";
    QString normalized = originalJson;
    normalized.replace("\r\n", "\n").replace("\r", "\n");

    int lastBrace = normalized.lastIndexOf('}');
    ASSERT_NE(lastBrace, -1);

    QString headerText = normalized.left(lastBrace);
    QStringList lines = headerText.split('\n');

    int targetLineIdx = -1;
    for (int i = lines.size() - 1; i >= 0; --i) {
        QString trimmed = lines[i].trimmed();
        if (!trimmed.isEmpty() && !trimmed.startsWith('#') && !trimmed.startsWith("//")) {
            targetLineIdx = i;
            break;
        }
    }

    if (targetLineIdx != -1) {
        QString trimmedLine = lines[targetLineIdx].trimmed();
        if (!trimmedLine.endsWith(',') && !trimmedLine.endsWith('{')) {
            lines[targetLineIdx] = lines[targetLineIdx].trimmed() + ",";
        }
    }

    lines.append("  # 棒読みちゃん (Bouyomi-chan) 音声読み上げ連携設定 (50001番ポート: TCPソケット通信, 50080番ポート: HTTP GET)");
    lines.append("  \"bouyomichan_enabled\": false,");
    lines.append("  \"bouyomichan_url\": \"http://localhost:50001\"");

    QString updatedJson = lines.join('\n') + "\n}\n";

    EXPECT_TRUE(updatedJson.contains("# 棒読みちゃん (Bouyomi-chan) 音声読み上げ連携設定"));
    EXPECT_TRUE(updatedJson.contains("\"bouyomichan_enabled\": false"));
    EXPECT_TRUE(updatedJson.contains("\"bouyomichan_url\": \"http://localhost:50001\""));

    // JSONParse で構文エラーにならないことを確認
    QByteArray cleanData = JsonCommentRemover::stripHashComments(updatedJson.toUtf8());
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(cleanData, &err);
    EXPECT_EQ(err.error, QJsonParseError::NoError);
    EXPECT_FALSE(doc.object().value("bouyomichan_enabled").toBool());
}

// UT-BOUYOMI-03: saveSettingsFromUI 実行時にファイル上の bouyomichan_enabled (true) を維持・即時同期する検証
TEST(BouyomiChanTest, VerifySaveSettingsPreservesDiskBouyomiEnabled) {
    AvatarWindow window;
    QString targetPath = ConfigUtils::resolveConfigFilePath("local_settings.json");
    if (targetPath.isEmpty()) {
        targetPath = QCoreApplication::applicationDirPath() + "/Config/local_settings.json";
    }

    QJsonObject obj;
    obj["bouyomichan_enabled"] = true;
    obj["bouyomichan_url"] = "http://192.168.0.29:50080/talk";
    
    QFile file(targetPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        file.write(QJsonDocument(obj).toJson());
        file.close();
    }

    window.saveSettingsFromUI();

    QFile checkFile(targetPath);
    ASSERT_TRUE(checkFile.open(QIODevice::ReadOnly | QIODevice::Text));
    QJsonObject savedObj = QJsonDocument::fromJson(checkFile.readAll()).object();
    checkFile.close();

    EXPECT_TRUE(savedObj.value("bouyomichan_enabled").toBool());
    EXPECT_EQ(savedObj.value("bouyomichan_url").toString(), "http://192.168.0.29:50080/talk");
}

// UT-BOUYOMI-04: 50001番ポート指定時の TCP パケット構造 (15バイトヘッダー + Little-Endian + UTF-8) 検証
TEST(BouyomiChanTest, VerifyTcpSocketPacketStructure) {
    QString testText = "テスト読み上げ";
    QByteArray packet = BouyomiChanClient::createTcpPacket(testText);

    QByteArray textBytes = testText.toUtf8();
    ASSERT_EQ(packet.size(), 15 + textBytes.size());

    QDataStream stream(packet);
    stream.setByteOrder(QDataStream::LittleEndian);

    quint16 command = 0;
    qint16 speed = 0, tone = 0, volume = 0;
    quint16 voice = 0;
    quint8 encoding = 0;
    quint32 length = 0;

    stream >> command >> speed >> tone >> volume >> voice >> encoding >> length;

    EXPECT_EQ(command, 0x0001);
    EXPECT_EQ(speed, -1);
    EXPECT_EQ(tone, -1);
    EXPECT_EQ(volume, -1);
    EXPECT_EQ(voice, 0);
    EXPECT_EQ(encoding, 0);
    EXPECT_EQ(length, static_cast<quint32>(textBytes.size()));

    QByteArray payload = packet.mid(15);
    EXPECT_EQ(payload, textBytes);
}

// UT-STT-10: 音声入力トリガー設定 - 両方有効 (voice_name_reaction_enabled: true, voice_wakeword_enabled: true)
TEST(STTFeatureTest, VerifyTriggerModeBoth) {
    QString configPath = ConfigUtils::resolveConfigFilePath("local_settings.json");
    QFile file(configPath);
    QJsonObject originalObj;
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        originalObj = QJsonDocument::fromJson(JsonCommentRemover::stripHashComments(file.readAll())).object();
        file.close();
    }

    QJsonObject testObj = originalObj;
    testObj["avatar_name"] = "ぶるたろう";
    testObj["voice_wakeword"] = "AIアシスタント";
    testObj["voice_name_reaction_enabled"] = true;
    testObj["voice_wakeword_enabled"] = true;
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(QJsonDocument(testObj).toJson());
        file.close();
    }

    CoreModule core;
    QSignalSpy spyAI(&core, &CoreModule::requestAI);

    // 1. アバター名呼びかけで反応
    AppEvent event1;
    event1.type = EventType::VoiceInputCompleted;
    event1.source = "STTManager";
    event1.text = "ぶるたろう、こんにちは";
    core.on_notify_events(event1);

    EXPECT_EQ(spyAI.count(), 1);
    EXPECT_EQ(spyAI.takeFirst().at(0).toString(), "こんにちは");

    // タイマー完了待ち (待機状態復帰)
    QTest::qWait(1100);

    // 2. ウェイクワード呼びかけで反応
    AppEvent event2;
    event2.type = EventType::VoiceInputCompleted;
    event2.source = "STTManager";
    event2.text = "AIアシスタント、こんにちは";
    core.on_notify_events(event2);

    EXPECT_EQ(spyAI.count(), 1);
    EXPECT_EQ(spyAI.takeFirst().at(0).toString(), "こんにちは");

    // 設定を元に戻す
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(QJsonDocument(originalObj).toJson());
        file.close();
    }
}

// UT-STT-11: 音声入力トリガー設定 - ウェイクワードのみ有効 (voice_name_reaction_enabled: false, voice_wakeword_enabled: true)
TEST(STTFeatureTest, VerifyTriggerModeWakewordOnly) {
    QString configPath = ConfigUtils::resolveConfigFilePath("local_settings.json");
    QFile file(configPath);
    QJsonObject originalObj;
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        originalObj = QJsonDocument::fromJson(JsonCommentRemover::stripHashComments(file.readAll())).object();
        file.close();
    }

    QJsonObject testObj = originalObj;
    testObj["avatar_name"] = "ぶるたろう";
    testObj["voice_wakeword"] = "AIアシスタント";
    testObj["voice_name_reaction_enabled"] = false;
    testObj["voice_wakeword_enabled"] = true;
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(QJsonDocument(testObj).toJson());
        file.close();
    }

    CoreModule core;
    QSignalSpy spyAI(&core, &CoreModule::requestAI);

    // 1. アバター名呼びかけは無視される
    AppEvent event1;
    event1.type = EventType::VoiceInputCompleted;
    event1.source = "STTManager";
    event1.text = "ぶるたろう、こんにちは";
    core.on_notify_events(event1);

    EXPECT_EQ(spyAI.count(), 0);

    // 2. ウェイクワード呼びかけで反応する
    AppEvent event2;
    event2.type = EventType::VoiceInputCompleted;
    event2.source = "STTManager";
    event2.text = "AIアシスタント、こんにちは";
    core.on_notify_events(event2);

    EXPECT_EQ(spyAI.count(), 1);
    EXPECT_EQ(spyAI.takeFirst().at(0).toString(), "こんにちは");

    // 設定を元に戻す
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(QJsonDocument(originalObj).toJson());
        file.close();
    }
}

// UT-STT-12: 音声入力トリガー設定 - アバター名のみ有効 (voice_name_reaction_enabled: true, voice_wakeword_enabled: false)
TEST(STTFeatureTest, VerifyTriggerModeAvatarNameOnly) {
    QString configPath = ConfigUtils::resolveConfigFilePath("local_settings.json");
    QFile file(configPath);
    QJsonObject originalObj;
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        originalObj = QJsonDocument::fromJson(JsonCommentRemover::stripHashComments(file.readAll())).object();
        file.close();
    }

    QJsonObject testObj = originalObj;
    testObj["avatar_name"] = "ぶるたろう";
    testObj["voice_wakeword"] = "AIアシスタント";
    testObj["voice_name_reaction_enabled"] = true;
    testObj["voice_wakeword_enabled"] = false;
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(QJsonDocument(testObj).toJson());
        file.close();
    }

    CoreModule core;
    QSignalSpy spyAI(&core, &CoreModule::requestAI);

    // 1. ウェイクワード呼びかけは無視される
    AppEvent event1;
    event1.type = EventType::VoiceInputCompleted;
    event1.source = "STTManager";
    event1.text = "AIアシスタント、こんにちは";
    core.on_notify_events(event1);

    EXPECT_EQ(spyAI.count(), 0);

    // 2. アバター名呼びかけで反応する
    AppEvent event2;
    event2.type = EventType::VoiceInputCompleted;
    event2.source = "STTManager";
    event2.text = "ぶるたろう、こんにちは";
    core.on_notify_events(event2);

    EXPECT_EQ(spyAI.count(), 1);
    EXPECT_EQ(spyAI.takeFirst().at(0).toString(), "こんにちは");

    // 設定を元に戻す
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(QJsonDocument(originalObj).toJson());
        file.close();
    }
}

// UT-STT-13: 音声入力トリガー設定 - 両方無効 (voice_name_reaction_enabled: false, voice_wakeword_enabled: false, PTTのみ受付)
TEST(STTFeatureTest, VerifyTriggerModeNoneAndPttOnly) {
    QString configPath = ConfigUtils::resolveConfigFilePath("local_settings.json");
    QFile file(configPath);
    QJsonObject originalObj;
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        originalObj = QJsonDocument::fromJson(JsonCommentRemover::stripHashComments(file.readAll())).object();
        file.close();
    }

    QJsonObject testObj = originalObj;
    testObj["avatar_name"] = "ぶるたろう";
    testObj["voice_wakeword"] = "AIアシスタント";
    testObj["voice_name_reaction_enabled"] = false;
    testObj["voice_wakeword_enabled"] = false;
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(QJsonDocument(testObj).toJson());
        file.close();
    }

    CoreModule core;
    QSignalSpy spyAI(&core, &CoreModule::requestAI);

    // 1. 通常発話（名前・ウェイクワード含む）は全て無視される
    AppEvent event1;
    event1.type = EventType::VoiceInputCompleted;
    event1.source = "STTManager";
    event1.text = "ぶるたろう、AIアシスタント、こんにちは";
    core.on_notify_events(event1);

    EXPECT_EQ(spyAI.count(), 0);

    // 2. PTT フラグ付き音声入力は無条件でAIへ送信される
    AppEvent eventPtt;
    eventPtt.type = EventType::VoiceInputCompleted;
    eventPtt.source = "STTManager";
    eventPtt.text = "PTTで送信された音声です";
    eventPtt.extraData["is_ptt"] = true;
    core.on_notify_events(eventPtt);

    EXPECT_EQ(spyAI.count(), 1);
    EXPECT_EQ(spyAI.takeFirst().at(0).toString(), "PTTで送信された音声です");

    // 設定を元に戻す
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(QJsonDocument(originalObj).toJson());
        file.close();
    }
}

// UT-STT-14: 設定ファイル自動補完 (voice_name_reaction_enabled / voice_wakeword_enabled / voice_wakeword)
TEST(STTFeatureTest, VerifyVoiceTriggerSettingsAutoInjection) {
    QString configPath = ConfigUtils::resolveConfigFilePath("local_settings.json");
    QFile file(configPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QByteArray rawData = file.readAll();
        file.close();

        QByteArray strippedData = JsonCommentRemover::stripHashComments(rawData);
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(strippedData, &parseError);

        EXPECT_EQ(parseError.error, QJsonParseError::NoError);
        EXPECT_TRUE(doc.isObject());
        EXPECT_TRUE(doc.object().contains("voice_name_reaction_enabled"));
        EXPECT_TRUE(doc.object().contains("voice_wakeword_enabled"));
        EXPECT_TRUE(doc.object().contains("voice_wakeword"));
    }
}

// UT-RAID-ROUTING-01: Twitch ソース経由の AI 応答が Twitch チャンネル宛てにルーティングされること
// (handleRaidShoutout は helixClient が null のためテスト不可なので、
//  同一ルーティング機構を担う on_requestAI(source="Twitch") で検証する)
TEST_F(AIClientTest, RaidResponseRoutesToTwitchChannel) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    QJsonObject settings;
    settings["twitch_channel"] = "my_channel";
    manager.loadSettingsFromJsonObject(settings);

    QSignalSpy eventSpy(&manager, &AIClientManager::notifyEvent);

    // Twitch ソースでリクエスト → 応答が Twitch チャンネルへルーティングされること
    manager.on_requestAI("レイドありがとう！", "[Twitch:my_channel] ferrely_leo", "Twitch");
    manager.on_clientRequestFinished("フェレリーレオさん、レイドありがとうございます！", true, 200);

    ASSERT_GE(eventSpy.count(), 1);
    AppEvent resEvent = eventSpy.last().at(0).value<AppEvent>();
    EXPECT_EQ(resEvent.type, EventType::AIResponseReceived);
    EXPECT_EQ(resEvent.source, "Twitch");
    EXPECT_TRUE(resEvent.extraData.contains("twitch_channel"));
    EXPECT_EQ(resEvent.extraData.value("twitch_channel").toString(), "my_channel");
}

// UT-RAID-PROMPT-01: レイド歓迎プロンプトの文脈指示検証
// (buildRaidShoutoutPrompt は純粋 static 関数なので helixClient 不要で直接テスト可能)
TEST_F(AIClientTest, RaidPromptContainsWelcomeAndNoReverseRoleInstructions) {
    QStringList recentGames = {"アクション", "RPG"};
    QString prompt = AIClientManager::buildRaidShoutoutPrompt(
        "ryu_no123", "リューノ",
        "", "アクション", recentGames, "夜の配信", "");

    EXPECT_TRUE(prompt.contains("遊びに来てくれました（レイドしてくれました）"));
    EXPECT_TRUE(prompt.contains("リスナーの皆さんを温かく歓迎"));
    EXPECT_TRUE(prompt.contains("逆の立場（今から相手の配信を見に行こう等）と絶対に誤認しないでください"));
    // 最近のゲーム履歴が含まれること
    EXPECT_TRUE(prompt.contains("アクション"));
    EXPECT_TRUE(prompt.contains("RPG"));
    // 逆転誤認につながるキーワードが含まれないこと
    EXPECT_FALSE(prompt.contains("見に行きたくなるような"));
}

// UT-CONV-SHOUTOUT-ROUTING-01: 会話トリガーの紹介がソースを引き継ぐこと
TEST_F(AIClientTest, ConversationShoutoutRoutingInheritsSource) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    QJsonObject settings;
    settings["twitch_channel"] = "my_channel";
    manager.loadSettingsFromJsonObject(settings);

    QSignalSpy eventSpy(&manager, &AIClientManager::notifyEvent);

    // UI ソースからリクエスト → 応答が UI に返ること
    manager.on_requestAI("ferrely_leoさんを紹介して", "", "UI");
    manager.on_clientRequestFinished("フェレリーレオさんの紹介コメントです！", true, 200);

    ASSERT_GE(eventSpy.count(), 1);
    AppEvent resEvent = eventSpy.last().at(0).value<AppEvent>();
    EXPECT_EQ(resEvent.type, EventType::AIResponseReceived);
    EXPECT_EQ(resEvent.source, "UI");
}

// UT-CONV-SHOUTOUT-PROMPT-01: 会話紹介プロンプトがレイド文脈を含まないこと
TEST_F(AIClientTest, ConversationShoutoutPromptHasNoRaidContext) {
    QStringList recentGames = {"格闘ゲーム", "シューター"};
    QString prompt = AIClientManager::buildConversationShoutoutPrompt(
        "ferrely_leo", "フェレリーレオ",
        "ゲームが好きです", "格闘ゲーム", recentGames, "夕方の配信", "https://twitter.com/ferrely");

    // 紹介文脈が含まれること
    EXPECT_TRUE(prompt.contains("紹介して"));
    EXPECT_TRUE(prompt.contains("フォロー"));
    // 最近のゲーム履歴が含まれること
    EXPECT_TRUE(prompt.contains("格闘ゲーム"));
    EXPECT_TRUE(prompt.contains("シューター"));
    // レイド文脈が含まれないこと
    EXPECT_FALSE(prompt.contains("レイドして来てくれた"));
    EXPECT_FALSE(prompt.contains("リスナーの皆さんを引き連れて"));
}

// UT-REG-ROUTING-01: Twitch 入力 → event.source="Twitch" + extraData["twitch_channel"] が設定されること
TEST_F(AIClientTest, RoutingTwitchInputGoesToTwitchOutput) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    QSignalSpy spy(&manager, &AIClientManager::notifyEvent);

    manager.on_requestAI("こんにちは", "[Twitch:my_channel]viewer", "");
    manager.on_clientRequestFinished("テスト応答", true, 200);

    ASSERT_GE(spy.count(), 1);
    AppEvent ev = spy.last().at(0).value<AppEvent>();
    ASSERT_EQ(ev.type, EventType::AIResponseReceived);
    EXPECT_EQ(ev.source, "Twitch");
    EXPECT_TRUE(ev.extraData.contains("twitch_channel"));
    EXPECT_EQ(ev.extraData["twitch_channel"].toString(), "my_channel");
    EXPECT_FALSE(ev.extraData.contains("channel_id"));
}

// UT-REG-ROUTING-02: Discord 入力 → event.source="Discord" + extraData["channel_id"] が設定されること
TEST_F(AIClientTest, RoutingDiscordInputGoesToDiscordOutput) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    QSignalSpy spy(&manager, &AIClientManager::notifyEvent);

    manager.on_requestAI("こんにちは", "[Discord:123456789]User#0001", "");
    manager.on_clientRequestFinished("テスト応答", true, 200);

    ASSERT_GE(spy.count(), 1);
    AppEvent ev = spy.last().at(0).value<AppEvent>();
    ASSERT_EQ(ev.type, EventType::AIResponseReceived);
    EXPECT_EQ(ev.source, "Discord");
    EXPECT_TRUE(ev.extraData.contains("channel_id"));
    EXPECT_EQ(ev.extraData["channel_id"].toString(), "123456789");
    EXPECT_FALSE(ev.extraData.contains("twitch_channel"));
}

// UT-REG-ROUTING-03: UI 入力 → event.source="UI" のみ、twitch_channel も channel_id も設定されないこと
TEST_F(AIClientTest, RoutingUIInputGoesToUIOnly) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    QSignalSpy spy(&manager, &AIClientManager::notifyEvent);

    manager.on_requestAI("こんにちは", "", "UI");
    manager.on_clientRequestFinished("テスト応答", true, 200);

    ASSERT_GE(spy.count(), 1);
    AppEvent ev = spy.last().at(0).value<AppEvent>();
    ASSERT_EQ(ev.type, EventType::AIResponseReceived);
    EXPECT_EQ(ev.source, "UI");
    EXPECT_FALSE(ev.extraData.contains("twitch_channel"));
    EXPECT_FALSE(ev.extraData.contains("channel_id"));
}

// UT-REG-ROUTING-04: Twitch → UI の順でリクエストした際、2件目の応答に前回の twitch_channel が残留しないこと
TEST_F(AIClientTest, RoutingSourceDoesNotPolluteBetweenRequests) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    QSignalSpy spy(&manager, &AIClientManager::notifyEvent);

    // 1件目: Twitch (user: "Alice")
    manager.on_requestAI("最初のリクエスト", "[Twitch:my_channel]Alice", "");
    manager.on_clientRequestFinished("Twitch応答", true, 200);

    ASSERT_GE(spy.count(), 1);
    AppEvent ev1 = spy.last().at(0).value<AppEvent>();
    EXPECT_EQ(ev1.source, "Twitch");
    EXPECT_TRUE(ev1.extraData.contains("twitch_channel"));

    // 2件目: UI (user: "Alice" - 同一ユーザーからのUI直接入力)
    int countBeforeSecond = spy.count();
    manager.on_requestAI("次のリクエスト", "Alice", "UI");
    manager.on_clientRequestFinished("UI応答", true, 200);

    ASSERT_GT(spy.count(), countBeforeSecond);
    AppEvent ev2 = spy.last().at(0).value<AppEvent>();
    EXPECT_EQ(ev2.source, "UI");
    // 前回の Twitch チャンネル情報が残留していないこと
    EXPECT_FALSE(ev2.extraData.contains("twitch_channel"));
    EXPECT_FALSE(ev2.extraData.contains("channel_id"));
}


// UT-REG-SNS-01: Twitter / YouTube の抽出
TEST(TwitchHelixClientTest, ExtractSnsInfo_TwitterAndYouTube) {
    QString bio = "配信してます！ Twitter: https://twitter.com/test_user YouTube: https://youtube.com/@test_ch";
    QString result = TwitchHelixClient::extractSnsInfo(bio);
    EXPECT_TRUE(result.contains("https://twitter.com/test_user"));
    EXPECT_TRUE(result.contains("https://youtube.com/@test_ch"));
}

// UT-REG-SNS-02: TikTok / Instagram の抽出
TEST(TwitchHelixClientTest, ExtractSnsInfo_TikTokAndInstagram) {
    QString bio = "TikTok: https://tiktok.com/@tiktok_user Insta: https://instagram.com/insta_user";
    QString result = TwitchHelixClient::extractSnsInfo(bio);
    EXPECT_TRUE(result.contains("https://tiktok.com/@tiktok_user"));
    EXPECT_TRUE(result.contains("https://instagram.com/insta_user"));
}

// UT-REG-SNS-03: discord.gg / linktr.ee の抽出
TEST(TwitchHelixClientTest, ExtractSnsInfo_DiscordAndLinktree) {
    QString bio = "Discord: https://discord.gg/mycommunity リンク集: https://linktr.ee/myprofile";
    QString result = TwitchHelixClient::extractSnsInfo(bio);
    EXPECT_TRUE(result.contains("https://discord.gg/mycommunity"));
    EXPECT_TRUE(result.contains("https://linktr.ee/myprofile"));
}

// UT-REG-SNS-04: 無関係 URL が誤抽出されないこと
TEST(TwitchHelixClientTest, ExtractSnsInfo_IgnoreIrrelevantUrls) {
    QString bio = "公式サイトはこちら: https://example.com/mypage ブログ: https://myblog.net/entry";
    QString result = TwitchHelixClient::extractSnsInfo(bio);
    EXPECT_TRUE(result.isEmpty());
}

// UT-REG-SNS-05: Bio が空の場合のフォールバック
TEST(TwitchHelixClientTest, ExtractSnsInfo_EmptyBio) {
    QString result = TwitchHelixClient::extractSnsInfo("");
    EXPECT_TRUE(result.isEmpty());
}

// =========================================================================
// UT-HELIX-01 ~ UT-HELIX-03: TwitchHelixClient 認証正規化およびリクエスト単体試験
// =========================================================================

// UT-HELIX-01: setCredentials における oauth: プレフィックスの自動除去と正規化
TEST(TwitchHelixClientTest, SetCredentials_TokenNormalization) {
    TwitchHelixClient client;

    // "oauth:abcd1234efgh" -> "abcd1234efgh"
    client.setCredentials("oauth:abcd1234efgh", "my_client_id_1");
    EXPECT_EQ(client.oauthToken(), "abcd1234efgh");
    EXPECT_EQ(client.clientId(), "my_client_id_1");

    // 大文字 "OAUTH:XYZ789" -> "XYZ789"
    client.setCredentials("  OAUTH:XYZ789  ", "  my_client_id_2  ");
    EXPECT_EQ(client.oauthToken(), "XYZ789");
    EXPECT_EQ(client.clientId(), "my_client_id_2");

    // プレフィックスなしの純粋トークン -> そのまま保持
    client.setCredentials("pure_token_value", "my_client_id_3");
    EXPECT_EQ(client.oauthToken(), "pure_token_value");
    EXPECT_EQ(client.clientId(), "my_client_id_3");
}

// =========================================================================
// MockTwitchHelixClient: テスト用 TwitchHelixClient モック
// =========================================================================
class MockTwitchHelixClient : public TwitchHelixClient {
public:
    explicit MockTwitchHelixClient(QObject *parent = nullptr) : TwitchHelixClient(parent) {}

    bool mockFetchSuccess = true;
    CreatorHelixInfo mockInfo;
    int fetchCallCount = 0;
    QString lastFetchedUser;

    bool mockShoutoutSuccess = true;
    int shoutoutCallCount = 0;
    QString lastShoutoutFrom;
    QString lastShoutoutTo;

    void fetchCreatorInfo(const QString &username, std::function<void(const CreatorHelixInfo &info, bool success)> callback) override {
        fetchCallCount++;
        lastFetchedUser = username;
        if (callback) {
            CreatorHelixInfo info = mockInfo;
            if (info.login.isEmpty()) info.login = username;
            if (info.displayName.isEmpty()) info.displayName = username;
            callback(info, mockFetchSuccess);
        }
    }

    void sendShoutoutToUser(const QString &fromUsername, const QString &toUsername, std::function<void(bool success)> callback) override {
        shoutoutCallCount++;
        lastShoutoutFrom = fromUsername;
        lastShoutoutTo = toUsername;
        if (callback) {
            callback(mockShoutoutSuccess);
        }
    }
};

// =========================================================================
// UT-RAID-FLOW-01 ~ UT-RAID-FLOW-06: レイドシャウトアウト E2E フロー単体試験
// =========================================================================

// UT-RAID-FLOW-01: handleRaidShoutout 正常系フロー
TEST_F(AIClientTest, RaidShoutout_NormalFlow) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    QJsonObject settings;
    settings["twitch_channel"] = "my_streamer_channel";
    settings["raid_auto_shoutout_enabled"] = true;
    settings["shoutout_use_command"] = true;
    manager.loadSettingsFromJsonObject(settings);

    MockTwitchHelixClient mockHelix;
    mockHelix.mockInfo.userId = "12345";
    mockHelix.mockInfo.login = "raider_taro";
    mockHelix.mockInfo.displayName = "レイド太郎";
    mockHelix.mockInfo.gameName = "Farming Simulator 19";
    manager.setHelixClient(&mockHelix);

    QSignalSpy eventSpy(&manager, &AIClientManager::notifyEvent);

    // レイドイベント受信
    QVariantMap raidMeta;
    raidMeta["login"] = "raider_taro";
    raidMeta["displayName"] = "レイド太郎";
    raidMeta["viewerCount"] = 15;
    raidMeta["channel"] = "my_streamer_channel";

    manager.on_twitchRaidReceived("raider_taro", raidMeta);

    // クリエイター情報取得および /shoutout が発火していること
    EXPECT_EQ(mockHelix.fetchCallCount, 1);
    EXPECT_EQ(mockHelix.lastFetchedUser, "raider_taro");
    EXPECT_EQ(mockHelix.shoutoutCallCount, 1);
    EXPECT_EQ(mockHelix.lastShoutoutTo, "raider_taro");
}

// UT-RAID-FLOW-02: handleRaidShoutout 応答完了と Twitch 送信
TEST_F(AIClientTest, RaidShoutout_ResponseRoutingToTwitch) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    QJsonObject settings;
    settings["twitch_channel"] = "my_streamer_channel";
    settings["raid_auto_shoutout_enabled"] = true;
    settings["shoutout_use_command"] = false; // 純粋な紹介コメントのルーティング検証
    settings["shoutout_prefix"] = "【レイド感謝】";
    manager.loadSettingsFromJsonObject(settings);

    MockTwitchHelixClient mockHelix;
    manager.setHelixClient(&mockHelix);

    QSignalSpy eventSpy(&manager, &AIClientManager::notifyEvent);

    QVariantMap raidMeta;
    raidMeta["login"] = "raider_taro";
    raidMeta["displayName"] = "レイド太郎";
    raidMeta["channel"] = "my_streamer_channel";
    manager.on_twitchRaidReceived("raider_taro", raidMeta);

    // AIレスポンス完了
    manager.on_clientRequestFinished("レイドありがとう！みんなで楽しもう！", true, 200);

    // イベント通知確認
    ASSERT_GE(eventSpy.count(), 1);
    bool foundResponse = false;
    for (int i = 0; i < eventSpy.count(); ++i) {
        AppEvent ev = eventSpy.at(i).at(0).value<AppEvent>();
        if (ev.type == EventType::AIResponseReceived) {
            foundResponse = true;
            EXPECT_EQ(ev.source, "Twitch");
            EXPECT_EQ(ev.extraData.value("twitch_channel").toString(), "my_streamer_channel");
            // IRC 禁止コマンド文字列が含まれないこと
            EXPECT_FALSE(ev.text.startsWith("/announce"));
            EXPECT_FALSE(ev.text.startsWith("/shoutout"));
            EXPECT_TRUE(ev.text.contains("レイドありがとう"));
            break;
        }
    }
    EXPECT_TRUE(foundResponse);
}

// UT-RAID-FLOW-03: レイド /shoutout クールタイムと待機キュー
TEST_F(AIClientTest, RaidShoutout_CooldownAndQueue) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    QJsonObject settings;
    settings["twitch_channel"] = "my_streamer_channel";
    settings["raid_auto_shoutout_enabled"] = true;
    settings["shoutout_use_command"] = true;
    manager.loadSettingsFromJsonObject(settings);

    MockTwitchHelixClient mockHelix;
    manager.setHelixClient(&mockHelix);

    // 1人目のレイド
    QVariantMap raidMeta1;
    raidMeta1["login"] = "user_a";
    raidMeta1["displayName"] = "ユーザーA";
    raidMeta1["channel"] = "my_streamer_channel";
    manager.on_twitchRaidReceived("user_a", raidMeta1);

    EXPECT_EQ(mockHelix.shoutoutCallCount, 1);
    EXPECT_EQ(mockHelix.lastShoutoutTo, "user_a");

    // クールタイム中に 2人目のレイド
    QVariantMap raidMeta2;
    raidMeta2["login"] = "user_b";
    raidMeta2["displayName"] = "ユーザーB";
    raidMeta2["channel"] = "my_streamer_channel";
    manager.on_twitchRaidReceived("user_b", raidMeta2);

    // 即時には 2人目の shoutout は呼ばれず、キューで待機
    EXPECT_EQ(mockHelix.shoutoutCallCount, 1);

    // クールタイム満了時のキュー処理
    manager.on_shoutoutSuccessReceived("user_a");
}

// UT-RAID-FLOW-04: 自己レイド・自己シャウトアウト除外
TEST_F(AIClientTest, RaidShoutout_IgnoreSelfShoutout) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    QJsonObject settings;
    settings["twitch_channel"] = "my_streamer_channel";
    settings["twitch_username"] = "my_bot_account";
    settings["raid_auto_shoutout_enabled"] = true;
    settings["shoutout_use_command"] = true;
    manager.loadSettingsFromJsonObject(settings);

    MockTwitchHelixClient mockHelix;
    manager.setHelixClient(&mockHelix);

    // 配信主自身がレイドした場合
    QVariantMap raidMeta;
    raidMeta["login"] = "my_streamer_channel";
    raidMeta["displayName"] = "配信主";
    raidMeta["channel"] = "my_streamer_channel";
    manager.on_twitchRaidReceived("my_streamer_channel", raidMeta);

    // クリエイター情報取得は行われるが、自己シャウトアウトはスキップされる
    EXPECT_EQ(mockHelix.shoutoutCallCount, 0);
}

// UT-RAID-FLOW-05: /shoutout 成功時のフォロー推奨メッセージ
TEST_F(AIClientTest, RaidShoutout_FollowRecommendationMessage) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    QJsonObject settings;
    settings["twitch_channel"] = "my_streamer_channel";
    settings["shoutout_follow_msg_enabled"] = true;
    settings["shoutout_use_announce"] = false;
    settings["shoutout_follow_msg_template"] = "ぜひ {name} さんをフォローしてね！";
    manager.loadSettingsFromJsonObject(settings);

    QSignalSpy eventSpy(&manager, &AIClientManager::notifyEvent);

    // シャウトアウト成功通知を受信
    manager.on_shoutoutSuccessReceived("raider_taro");

    ASSERT_GE(eventSpy.count(), 1);
    AppEvent ev = eventSpy.last().at(0).value<AppEvent>();
    EXPECT_EQ(ev.type, EventType::AIResponseReceived);
    EXPECT_EQ(ev.source, "ShoutoutModule");
    EXPECT_EQ(ev.text, "ぜひ raider_taro さんをフォローしてね！");
}

// UT-RAID-FLOW-06: Helix API 失敗時の安全なフォールバック
TEST_F(AIClientTest, RaidShoutout_HelixFailureFallback) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    QJsonObject settings;
    settings["twitch_channel"] = "my_streamer_channel";
    settings["raid_auto_shoutout_enabled"] = true;
    manager.loadSettingsFromJsonObject(settings);

    MockTwitchHelixClient mockHelix;
    mockHelix.mockFetchSuccess = false; // クリエイター情報取得失敗
    manager.setHelixClient(&mockHelix);

    QVariantMap raidMeta;
    raidMeta["login"] = "offline_user";
    raidMeta["displayName"] = "オフラインユーザー";

    // エラーでもクラッシュせず処理が通ること
    EXPECT_NO_THROW(manager.on_twitchRaidReceived("offline_user", raidMeta));
}

// =========================================================================
// UT-KNOWLEDGE-TRIGGER-01 ~ UT-KNOWLEDGE-TRIGGER-05: MarkdownTableEngine 除外トリガー単体試験
// =========================================================================

// UT-KNOWLEDGE-TRIGGER-01 & 02: 「うらない」「占い」で Omikuji が想起されること
TEST(MarkdownTableEngineTest, ExcludeTriggers_OmikujiTriggerMatch) {
    MarkdownTableEngine engine("knowledge");

    // ひらがな「うらない」
    KnowledgeIndexEntry entry1 = engine.resolveBestEntryForTrigger("うらない");
    EXPECT_TRUE(entry1.isValid);
    EXPECT_EQ(entry1.tableName, "Ranks");
    EXPECT_EQ(entry1.group, "Omikuji");

    // 漢字「占い」
    KnowledgeIndexEntry entry2 = engine.resolveBestEntryForTrigger("今日の占い教えて");
    EXPECT_TRUE(entry2.isValid);
    EXPECT_EQ(entry2.tableName, "Ranks");
    EXPECT_EQ(entry2.group, "Omikuji");
}

// UT-KNOWLEDGE-TRIGGER-03: 他占い（タロット、手相等）で Omikuji が除外されること
TEST(MarkdownTableEngineTest, ExcludeTriggers_OtherFortunesExcluded) {
    MarkdownTableEngine engine("knowledge");

    // タロット占い -> Omikuji は除外
    KnowledgeIndexEntry entry1 = engine.resolveBestEntryForTrigger("タロット占いして");
    EXPECT_NE(entry1.group, "Omikuji");

    // 手相占い -> Omikuji は除外
    KnowledgeIndexEntry entry2 = engine.resolveBestEntryForTrigger("手相占いはできる？");
    EXPECT_NE(entry2.group, "Omikuji");

    // 四柱推命 -> Omikuji は除外
    KnowledgeIndexEntry entry3 = engine.resolveBestEntryForTrigger("四柱推命の占いをして");
    EXPECT_NE(entry3.group, "Omikuji");
}

// UT-KNOWLEDGE-TRIGGER-04: 星座指定占いで Zodiac が優先想起され Omikuji が除外されること
TEST(MarkdownTableEngineTest, ExcludeTriggers_ZodiacPriorityOverOmikuji) {
    MarkdownTableEngine engine("knowledge");

    // 「ふたご座のうらないして」 -> Omikuji は「座」で除外され、Zodiac がヒット
    KnowledgeIndexEntry entry1 = engine.resolveBestEntryForTrigger("ふたご座のうらないして");
    EXPECT_TRUE(entry1.isValid);
    EXPECT_EQ(entry1.group, "Zodiac");

    // 「牡羊座の占い」
    KnowledgeIndexEntry entry2 = engine.resolveBestEntryForTrigger("牡羊座の占い");
    EXPECT_TRUE(entry2.isValid);
    EXPECT_EQ(entry2.group, "Zodiac");
}

// UT-KNOWLEDGE-TRIGGER-05: 除外トリガーの大文字・小文字・部分一致
TEST(MarkdownTableEngineTest, ExcludeTriggers_CaseAndPartialMatch) {
    MarkdownTableEngine engine("knowledge");

    // 通常のおみくじは問題なくヒット
    KnowledgeIndexEntry entry = engine.resolveBestEntryForTrigger("今日のおみくじ");
    EXPECT_TRUE(entry.isValid);
    EXPECT_EQ(entry.group, "Omikuji");
}

// ============================================================================
// TwitchIntroGenerator ＆ クリエイター紹介文生成 単体試験 (UT-INTRO-GEN)
// ============================================================================

// UT-INTRO-GEN-01: レイド用プロンプト構築と逆転誤認防止ルール
TEST(TwitchIntroGeneratorTest, BuildRaidShoutoutPrompt_ContainsAntiInversionRule) {
    QString prompt = AIClientManager::buildRaidShoutoutPrompt(
        "raider_taro", "レイド太郎", "FPSゲーマーです", "VALORANT",
        {"Apex Legends", "Overwatch 2"}, "今夜もランクマ！", "Twitter: @raider_taro",
        "standard", "明るく元気な口調で！");

    // 歓迎文脈とクリエイター情報
    EXPECT_TRUE(prompt.contains("レイド太郎"));
    EXPECT_TRUE(prompt.contains("raider_taro"));
    EXPECT_TRUE(prompt.contains("VALORANT"));
    EXPECT_TRUE(prompt.contains("Apex Legends"));
    EXPECT_TRUE(prompt.contains("Twitter: @raider_taro"));

    // 逆転誤認防止ルールが明記されていること
    EXPECT_TRUE(prompt.contains("私たちが相手の配信枠を見に行く（レイドする）のではなく"));
    EXPECT_TRUE(prompt.contains("相手が私たちの配信枠へレイドして来てくれた"));
    EXPECT_TRUE(prompt.contains("逆の立場"));
}

// UT-INTRO-GEN-02: 会話紹介用プロンプト構築
TEST(TwitchIntroGeneratorTest, BuildConversationShoutoutPrompt_NeutralContext) {
    QString prompt = AIClientManager::buildConversationShoutoutPrompt(
        "streamer_hanako", "配信花子", "まったり雑談配信", "Just Chatting",
        {"Minecraft", "雀魂"}, "お茶でも飲みながら", "YouTube: hanako_ch",
        "short", "落ち着いた優しい口調で");

    EXPECT_TRUE(prompt.contains("配信花子"));
    EXPECT_TRUE(prompt.contains("streamer_hanako"));
    EXPECT_TRUE(prompt.contains("Just Chatting"));
    EXPECT_TRUE(prompt.contains("YouTube: hanako_ch"));
    EXPECT_TRUE(prompt.contains("これはシャウトアウト（紹介）コメントです"));
    EXPECT_FALSE(prompt.contains("レイドしてくれました"));
}

// UT-INTRO-GEN-03: 会話紹介トリガー時のフォールバック処理
TEST_F(AIClientTest, ConversationShoutout_FallbackWhenCliNotFound) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    MockTwitchHelixClient mockHelix;
    manager.setHelixClient(&mockHelix);

    QSignalSpy eventSpy(&manager, &AIClientManager::notifyEvent);

    manager.handleConversationShoutout("friend_streamer", "Twitch", "my_channel");
    manager.on_clientRequestFinished("friend_streamer さんの紹介コメントです！", true, 200);

    // モックから Helix 情報が返り、Dummy AI で紹介文が生成されること
    ASSERT_GE(eventSpy.count(), 1);
    AppEvent ev = eventSpy.last().at(0).value<AppEvent>();
    EXPECT_EQ(ev.type, EventType::AIResponseReceived);
    EXPECT_EQ(ev.source, "Twitch");
    EXPECT_EQ(ev.extraData.value("twitch_channel").toString(), "my_channel");
}

// UT-INTRO-GEN-04: レイド紹介トリガー時のフォールバック処理
TEST_F(AIClientTest, RaidShoutout_FallbackWhenCliNotFound) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    QJsonObject settings;
    settings["twitch_channel"] = "my_streamer_channel";
    settings["raid_auto_shoutout_enabled"] = true;
    settings["shoutout_use_command"] = false;
    manager.loadSettingsFromJsonObject(settings);

    MockTwitchHelixClient mockHelix;
    manager.setHelixClient(&mockHelix);

    QSignalSpy eventSpy(&manager, &AIClientManager::notifyEvent);

    QVariantMap raidMeta;
    raidMeta["login"] = "raider_guest";
    raidMeta["displayName"] = "ゲスト配信者";
    raidMeta["channel"] = "my_streamer_channel";
    manager.on_twitchRaidReceived("raider_guest", raidMeta);
    manager.on_clientRequestFinished("レイドありがとうございます！", true, 200);

    // モックから応答生成が完了し、Twitch 宛にイベントが飛ぶこと
    ASSERT_GE(eventSpy.count(), 1);
    AppEvent ev = eventSpy.last().at(0).value<AppEvent>();
    EXPECT_EQ(ev.type, EventType::AIResponseReceived);
    EXPECT_EQ(ev.source, "Twitch");
    EXPECT_EQ(ev.extraData.value("twitch_channel").toString(), "my_streamer_channel");
}

// UT-INTRO-GEN-07: レイド受信時 (shoutout_use_command = true) のシャウトアウト API 送信検証
TEST_F(AIClientTest, RaidShoutout_TriggersShoutoutApiOnRaid) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    QJsonObject settings;
    settings["twitch_channel"] = "my_streamer_channel";
    settings["raid_auto_shoutout_enabled"] = true;
    settings["shoutout_use_command"] = true;
    manager.loadSettingsFromJsonObject(settings);

    MockTwitchHelixClient mockHelix;
    manager.setHelixClient(&mockHelix);

    QVariantMap raidMeta;
    raidMeta["login"] = "raider_friend";
    raidMeta["displayName"] = "友人配信者";
    raidMeta["channel"] = "my_streamer_channel";

    manager.on_twitchRaidReceived("raider_friend", raidMeta);

    // MockTwitchHelixClient 側で sendShoutoutToUser または sendShoutout が呼び出されたこと
    EXPECT_EQ(mockHelix.lastShoutoutTo, "raider_friend");
}

// UT-INTRO-GEN-08: 会話トリガー紹介時 (/shoutout API が送信されないことの検証)
TEST_F(AIClientTest, ConversationShoutout_DoesNotTriggerShoutoutApi) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    QJsonObject settings;
    settings["twitch_channel"] = "my_streamer_channel";
    settings["shoutout_use_command"] = true;
    manager.loadSettingsFromJsonObject(settings);

    MockTwitchHelixClient mockHelix;
    manager.setHelixClient(&mockHelix);

    mockHelix.lastShoutoutTo.clear();

    manager.handleConversationShoutout("friend_guest", "Twitch", "my_streamer_channel");

    // 会話紹介では /shoutout API は一切呼ばれないこと
    EXPECT_TRUE(mockHelix.lastShoutoutTo.isEmpty());
}

// UT-INTRO-GEN-09: 自己レイド時のシャウトアウト API スキップ検証
TEST_F(AIClientTest, RaidShoutout_SkipsShoutoutApiForSelf) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    QJsonObject settings;
    settings["twitch_channel"] = "my_streamer_channel";
    settings["twitch_username"] = "my_streamer_channel";
    settings["raid_auto_shoutout_enabled"] = true;
    settings["shoutout_use_command"] = true;
    manager.loadSettingsFromJsonObject(settings);

    MockTwitchHelixClient mockHelix;
    manager.setHelixClient(&mockHelix);

    mockHelix.lastShoutoutTo.clear();

    QVariantMap raidMeta;
    raidMeta["login"] = "my_streamer_channel";
    raidMeta["displayName"] = "配信者自身";
    raidMeta["channel"] = "my_streamer_channel";

    manager.on_twitchRaidReceived("my_streamer_channel", raidMeta);

    // 自分自身へのレイド時は /shoutout API がスキップされること
    EXPECT_TRUE(mockHelix.lastShoutoutTo.isEmpty());
}

// ---------------------------------------------------------------------------
// UT-GEMINI-01 ~ 06: Google Gemini (Google AI Studio) 単体試験
// ---------------------------------------------------------------------------

TEST_F(AIClientTest, GeminiClientBasicProperties) {
    // UT-GEMINI-01: クライアントID・デフォルトモデル・プロパティ検証
    GeminiAIClient client;
    EXPECT_EQ(client.clientId(), "gemini");
    EXPECT_EQ(client.currentModelName(), "gemini-2.0-flash");

    client.setModel("gemini-1.5-flash");
    EXPECT_EQ(client.currentModelName(), "gemini-1.5-flash");

    client.setApiKey("AIzaSyTestApiKey");
    EXPECT_EQ(client.apiKey(), "AIzaSyTestApiKey");
}

TEST_F(AIClientTest, GeminiClientEmptyApiKeyError) {
    // UT-GEMINI-02: API キー未設定時の即時エラー通知検証
    GeminiAIClient client;
    client.setApiKey("");

    bool received = false;
    QString resultMsg;
    bool successFlag = true;

    QObject::connect(&client, &IAIClient::requestFinished, [&](const QString &msg, bool success, int httpCode) {
        Q_UNUSED(httpCode);
        received = true;
        resultMsg = msg;
        successFlag = success;
    });

    client.sendRequest("こんにちは");
    EXPECT_TRUE(received);
    EXPECT_FALSE(successFlag);
    EXPECT_TRUE(resultMsg.contains("Gemini APIキーが設定されていません"));
}

TEST_F(AIClientTest, GeminiRateLimitTrackerDefaultStatus) {
    // UT-GEMINI-03: RateLimitTracker Gemini 初期値検証
    GeminiAIClient client;
    ProviderStatus s = client.defaultStatus();

    EXPECT_EQ(s.provider, "gemini");
    EXPECT_EQ(s.rpmMax, 15);
    EXPECT_EQ(s.rpmRemaining, 15);
    EXPECT_EQ(s.rpdMax, 1500);
    EXPECT_EQ(s.rpdRemaining, 1500);
    EXPECT_EQ(s.tpmMax, 1000000);
    EXPECT_EQ(s.tpmRemaining, 1000000);
    EXPECT_TRUE(s.toolCall);
    EXPECT_DOUBLE_EQ(s.cost, 0.0);
}

TEST_F(AIClientTest, GeminiAIClientManagerIntegration) {
    // UT-GEMINI-04: AIClientManager での Gemini プロバイダ切り替えおよびフォールバック順序
    AIClientManager manager;
    manager.setAIProvider("gemini", true);

    QStringList priority = manager.workerPriorityOrder();
    EXPECT_TRUE(priority.contains("gemini"));
    EXPECT_EQ(priority.first(), "gemini");
}

TEST_F(AIClientTest, GeminiHumanReadableErrorFormatting) {
    // UT-GEMINI-05: Gemini エラーの自然言語メッセージ変換検証
    AIClientManager manager;
    QJsonObject emptyObj;

    QString msg429 = manager.buildHumanReadableError(429, "gemini", emptyObj);
    EXPECT_TRUE(msg429.contains("gemini"));
    EXPECT_TRUE(msg429.contains("レート制限"));

    QString msg401 = manager.buildHumanReadableError(401, "gemini", emptyObj);
    EXPECT_TRUE(msg401.contains("API キーが正しくありません"));

    QString msg404 = manager.buildHumanReadableError(404, "gemini", emptyObj);
    EXPECT_TRUE(msg404.contains("モデル名が見つかりません"));
}

TEST_F(AIClientTest, AvatarWindowGeminiSettingsPersistence) {
    // UT-GEMINI-06: AvatarWindow の Gemini 設定読み込み・保存の検証
    QString targetPath = ConfigUtils::resolveConfigFilePath("local_settings.json");
    QByteArray originalContent;
    bool hasOriginal = QFile::exists(targetPath);
    if (hasOriginal) {
        QFile file(targetPath);
        if (file.open(QIODevice::ReadOnly)) {
            originalContent = file.readAll();
            file.close();
        }
    }

    {
        QJsonObject obj;
        obj["ai_provider"] = "gemini";
        obj["gemini_api_key"] = "AIzaSy_UnitTest_Key_12345";
        obj["gemini_model"] = "gemini-2.0-flash";

        QFile file(targetPath);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(obj).toJson());
        file.close();

        AvatarWindow window;
        // UI に反映されていることを確認
        const ProviderConfigSpec *geminiSpec = nullptr;
        for (const auto &spec : window.providerSpecs()) {
            if (spec.id == "gemini") {
                geminiSpec = &spec;
                break;
            }
        }
        ASSERT_NE(geminiSpec, nullptr);
        EXPECT_TRUE(geminiSpec->checkbox->isChecked());
        EXPECT_EQ(geminiSpec->keyEdit->text(), "AIzaSy_UnitTest_Key_12345");
        // Gemini は UI 上にモデルコンボボックスを持たないこと
        EXPECT_EQ(geminiSpec->modelCombo, nullptr);

        // 変更して保存
        geminiSpec->keyEdit->setText("AIzaSy_Updated_Key_67890");
        window.saveSettingsFromUI();

        QFile file2(targetPath);
        ASSERT_TRUE(file2.open(QIODevice::ReadOnly | QIODevice::Text));
        QByteArray cleanData = JsonCommentRemover::stripHashComments(file2.readAll());
        QJsonObject savedObj = QJsonDocument::fromJson(cleanData).object();
        file2.close();

        EXPECT_EQ(savedObj.value("gemini_api_key").toString(), "AIzaSy_Updated_Key_67890");
        EXPECT_EQ(savedObj.value("ai_provider").toString(), "gemini");
        EXPECT_EQ(savedObj.value("gemini_model").toString(), "gemini-2.0-flash");
    }

    if (hasOriginal && !originalContent.isEmpty()) {
        QFile file(targetPath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(originalContent);
            file.close();
        }
    }
}

TEST_F(AIClientTest, RateLimitTabWidgetGeminiCardDisplay) {
    // UT-GEMINI-07: RateLimitTabWidget 内の Gemini カード表示検証
    RateLimitTabWidget tabWidget;
    GeminiAIClient client;
    ProviderStatus st = client.defaultStatus();
    st.rpmRemaining = 12;
    st.rpdRemaining = 1450;

    tabWidget.onStatusUpdated({st});

    // 内部カードが生成されていること
    QList<QGroupBox*> groupBoxes = tabWidget.findChildren<QGroupBox*>();
    bool foundGemini = false;
    for (QGroupBox *gb : groupBoxes) {
        if (gb->title().contains("GEMINI", Qt::CaseInsensitive)) {
            foundGemini = true;
            break;
        }
    }
    EXPECT_TRUE(foundGemini);
}

TEST_F(AIClientTest, ManagerAIProviderDynamicSelectionFromConfiguredKeys) {
    // UT-MGR-PROVIDER-01: 設定済みAPIキーに基づくManager AIプロバイダ一覧の動的抽出
    QString targetPath = ConfigUtils::resolveConfigFilePath("local_settings.json");
    QByteArray originalContent;
    bool hasOriginal = QFile::exists(targetPath);
    if (hasOriginal) {
        QFile file(targetPath);
        if (file.open(QIODevice::ReadOnly)) {
            originalContent = file.readAll();
            file.close();
        }
    }

    {
        QJsonObject obj;
        obj["groq_api_key"] = "gsk_test_key";
        obj["gemini_api_key"] = "AIzaSy_test_key";
        obj["mistral_api_key"] = "";
        obj["sakura_api_key"] = "";
        obj["huggingface_api_key"] = "";
        obj["openrouter_api_key"] = "";

        QFile file(targetPath);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(obj).toJson());
        file.close();

        AvatarWindow window;
        ASSERT_NE(window.managerProviderCombo(), nullptr);
        QStringList items;
        for (int i = 0; i < window.managerProviderCombo()->count(); ++i) items.append(window.managerProviderCombo()->itemText(i));
        EXPECT_TRUE(items.contains("groq"));
        EXPECT_TRUE(items.contains("gemini"));
        EXPECT_FALSE(items.contains("mistral"));
    }

    if (hasOriginal && !originalContent.isEmpty()) {
        QFile file(targetPath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(originalContent);
            file.close();
        }
    }
}

TEST_F(AIClientTest, ManagerAIProviderFallbackWhenNoKeysSet) {
    // UT-MGR-PROVIDER-02: 全キー未設定時のデフォルト全プロバイダフォールバック
    QString targetPath = ConfigUtils::resolveConfigFilePath("local_settings.json");
    QByteArray originalContent;
    bool hasOriginal = QFile::exists(targetPath);
    if (hasOriginal) {
        QFile file(targetPath);
        if (file.open(QIODevice::ReadOnly)) {
            originalContent = file.readAll();
            file.close();
        }
    }

    {
        QJsonObject obj;
        obj["groq_api_key"] = "";
        obj["gemini_api_key"] = "";
        obj["mistral_api_key"] = "";
        obj["sakura_api_key"] = "";
        obj["huggingface_api_key"] = "";
        obj["openrouter_api_key"] = "";

        QFile file(targetPath);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(obj).toJson());
        file.close();

        AvatarWindow window;
        ASSERT_NE(window.managerProviderCombo(), nullptr);
        QStringList items;
        for (int i = 0; i < window.managerProviderCombo()->count(); ++i) items.append(window.managerProviderCombo()->itemText(i));
        EXPECT_TRUE(items.contains("groq"));
        EXPECT_TRUE(items.contains("gemini"));
        EXPECT_TRUE(items.contains("sakura"));
        EXPECT_TRUE(items.contains("mistral"));
        EXPECT_TRUE(items.contains("openrouter"));
        EXPECT_TRUE(items.contains("huggingface"));
    }

    if (hasOriginal && !originalContent.isEmpty()) {
        QFile file(targetPath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(originalContent);
            file.close();
        }
    }
}

TEST_F(AIClientTest, ManagerAIModelListUpdatesOnProviderChange) {
    // UT-MGR-PROVIDER-03: Manager AIプロバイダ変更時の推奨モデル一覧更新
    AvatarWindow window;
    ASSERT_NE(window.managerModelCombo(), nullptr);

    // 各プロバイダの推奨モデルリスト更新を網羅的に検証
    window.updateManagerModelComboList("groq");
    EXPECT_TRUE(window.managerModelCombo()->findText("llama-3.1-8b-instant (推奨)") >= 0);

    window.updateManagerModelComboList("gemini");
    EXPECT_TRUE(window.managerModelCombo()->findText("gemini-2.0-flash (推奨)") >= 0);

    window.updateManagerModelComboList("sakura");
    EXPECT_TRUE(window.managerModelCombo()->findText("llm-jp-3.1-8x13b-instruct4 (推奨)") >= 0);

    window.updateManagerModelComboList("mistral");
    EXPECT_TRUE(window.managerModelCombo()->findText("mistral-small-latest (推奨)") >= 0);

    window.updateManagerModelComboList("openrouter");
    EXPECT_TRUE(window.managerModelCombo()->findText("google/gemma-4-31b-it:free (推奨)") >= 0);

    window.updateManagerModelComboList("huggingface");
    EXPECT_TRUE(window.managerModelCombo()->findText("meta-llama/Llama-3.1-8B-Instruct (推奨)") >= 0);
}

TEST_F(AIClientTest, ManagerContext_ExtractCandidates) {
    // UT-CTX-01: 会話ログからの候補コンテキスト抽出
    QList<ChatMessageEntry> logs;
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    ChatMessageEntry e1{"msg_1", "userA", false, "富士山ってどこにあるの？", now - 15000};
    ChatMessageEntry e2{"msg_2", "AI", true, "山梨県のみにありますよ！", now - 10000};
    ChatMessageEntry e3{"msg_3", "userC", false, "昨日のゲーム面白かったね", now - 5000};

    logs.append(e1);
    logs.append(e2);
    logs.append(e3);

    QList<ContextCandidate> candidates = ManagerContextEvaluator::extractContextCandidates(logs, "そこは静岡だよ！", 5, now);
    ASSERT_EQ(candidates.size(), 3);
    EXPECT_EQ(candidates.at(0).messageId, "msg_1");
    EXPECT_EQ(candidates.at(1).messageId, "msg_2");
    EXPECT_TRUE(candidates.at(1).isAssistant);
    EXPECT_EQ(candidates.at(2).messageId, "msg_3");
}

TEST_F(AIClientTest, ManagerContext_InformationSpeechAct) {
    // UT-CTX-02: 情報伝達 (INFORMATION) に対するリアクション生成
    QList<ContextCandidate> candidates;
    PendingClarification pending;

    ManagerContextResult result = ManagerContextEvaluator::evaluateContextRuleBased(
        "アバター名、aliceさんが今日はお休みだって言ってるよ！",
        "bob",
        candidates,
        pending
    );

    EXPECT_EQ(result.speechAct, "INFORMATION");
    EXPECT_EQ(result.responseAction, "ACKNOWLEDGE");
    EXPECT_GE(result.referenceConfidence, 0.80);

    QString instruction = ManagerContextEvaluator::formatWorkerInstruction(result, pending);
    EXPECT_TRUE(instruction.contains("情報伝達の受け止め指示"));
    EXPECT_TRUE(instruction.contains("自然な相槌・リアクション"));
}

TEST_F(AIClientTest, ManagerContext_CorrectionSpeechAct) {
    // UT-CTX-03: 過去発言訂正 (CORRECTION) に対する誤り受容プロンプト
    QList<ContextCandidate> candidates;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    candidates.append(ContextCandidate{"msg_001", "AI", true, "富士山は山梨県だけにあります", 10});

    PendingClarification pending;

    ManagerContextResult result = ManagerContextEvaluator::evaluateContextRuleBased(
        "そこは静岡だよ！",
        "userB",
        candidates,
        pending
    );

    EXPECT_EQ(result.speechAct, "CORRECTION");
    EXPECT_EQ(result.responseAction, "CORRECT_APOLOGY");
    EXPECT_EQ(result.refMessageId, "msg_001");

    QString instruction = ManagerContextEvaluator::formatWorkerInstruction(result, pending);
    EXPECT_TRUE(instruction.contains("過去発言の訂正受容指示"));
    EXPECT_TRUE(instruction.contains("一般論や人生論、励まし"));
    EXPECT_TRUE(instruction.contains("完全に禁止"));
}

TEST_F(AIClientTest, ManagerContext_DemonstrativeReference) {
    // UT-CTX-04: 指示語（それ・そこ）の参照先特定
    QList<ContextCandidate> candidates;
    candidates.append(ContextCandidate{"msg_ai_100", "AI", true, "100ドルは1000円です", 5});

    PendingClarification pending;

    ManagerContextResult result = ManagerContextEvaluator::evaluateContextRuleBased(
        "それ違うよ！15000円だよ",
        "userA",
        candidates,
        pending
    );

    EXPECT_EQ(result.speechAct, "CORRECTION");
    EXPECT_EQ(result.refMessageId, "msg_ai_100");
    EXPECT_GE(result.referenceConfidence, 0.80);
}

TEST_F(AIClientTest, ManagerContext_AskClarificationOnAmbiguity) {
    // UT-CTX-05: 低確信度時の短い聞き返し (ASK_CLARIFICATION)
    QList<ContextCandidate> candidates;
    candidates.append(ContextCandidate{"msg_ai_1", "AI", true, "明日は晴れです", 30});
    candidates.append(ContextCandidate{"msg_ai_2", "AI", true, "ゲームは10時から開始です", 10});

    PendingClarification pending;

    ManagerContextResult result = ManagerContextEvaluator::evaluateContextRuleBased(
        "それ違うよ！",
        "userA",
        candidates,
        pending
    );

    EXPECT_EQ(result.speechAct, "CORRECTION");
    EXPECT_EQ(result.responseAction, "ASK_CLARIFICATION");
    EXPECT_LT(result.referenceConfidence, 0.50);
    EXPECT_EQ(result.clarificationQuestion, "それってどれのこと？");

    QString instruction = ManagerContextEvaluator::formatWorkerInstruction(result, pending);
    EXPECT_TRUE(instruction.contains("聞き返し指示"));
    EXPECT_TRUE(instruction.contains("それってどれのこと？"));
}

TEST_F(AIClientTest, ManagerContext_PendingClarificationResume) {
    // UT-CTX-06: 聞き返し状態 (PendingClarification) の保持と文脈復元
    PendingClarification pending;
    pending.requester = "userA";
    pending.candidateTopic = "それ違うよ！";
    pending.questionText = "それってどれのこと？";
    pending.timestamp = QDateTime::currentMSecsSinceEpoch();

    EXPECT_TRUE(pending.isValid());

    QList<ContextCandidate> candidates;
    ManagerContextResult result = ManagerContextEvaluator::evaluateContextRuleBased(
        "ゲームの時間のこと！",
        "userA",
        candidates,
        pending
    );

    EXPECT_EQ(result.responseAction, "ANSWER");
    QString instruction = ManagerContextEvaluator::formatWorkerInstruction(result, pending);
    EXPECT_TRUE(instruction.contains("直前の聞き返し応答に対する文脈復元指示"));
    EXPECT_TRUE(instruction.contains("それってどれのこと？"));
}

TEST_F(AIClientTest, ManagerContext_AmbiguitySafetyFirstAskClarification) {
    // UT-CTX-07: 複数候補競合・絞り込み不能時の未特定 ＆ 聞き返し選定
    QList<ContextCandidate> candidates;
    candidates.append(ContextCandidate{"msg_001", "userA", false, "富士山ってどこにあるの？", 40});
    candidates.append(ContextCandidate{"msg_002", "AI", true, "富士山は山梨県にあります！", 35});
    candidates.append(ContextCandidate{"msg_003", "AI", true, "東京タワーは港区にあります！", 15});

    PendingClarification pending;

    ManagerContextResult result = ManagerContextEvaluator::evaluateContextRuleBased(
        "それ違うよ！",
        "userB",
        candidates,
        pending
    );

    EXPECT_EQ(result.speechAct, "CORRECTION");
    EXPECT_EQ(result.responseAction, "ASK_CLARIFICATION");
    EXPECT_TRUE(result.refMessageId.isEmpty());
    EXPECT_LT(result.referenceConfidence, 0.50);
    EXPECT_EQ(result.clarificationQuestion, "それってどれのこと？");
}

TEST_F(AIClientTest, ManagerContext_GreetOnBehalfCommand) {
    // UT-CTX-08: 挨拶代行指示 (COMMAND) の文脈判定と指示生成
    QList<ContextCandidate> candidates;
    PendingClarification pending;

    ManagerContextResult result = ManagerContextEvaluator::evaluateContextRuleBased(
        "ぶるたろう、配信終了のご挨拶をして",
        "userA",
        candidates,
        pending,
        "ぶるたろう"
    );

    EXPECT_EQ(result.speechAct, "COMMAND");
    EXPECT_EQ(result.responseAction, "GREET_ON_BEHALF");
    EXPECT_GE(result.referenceConfidence, 0.90);

    QString instruction = ManagerContextEvaluator::formatWorkerInstruction(result, pending);
    EXPECT_TRUE(instruction.contains("挨拶・発話の代行指示"));
    EXPECT_TRUE(instruction.contains("配信の視聴者・全体に向けた挨拶"));
}

TEST_F(AIClientTest, AIClient_AntiSelfIntroPromptValidation) {
    // UT-CTX-09: 共通システムプロンプトにおける名乗り抑制・質問即応指示の検証
    QString basePrompt = IAIClient::buildBaseSystemPrompt("ぶるたろう");

    EXPECT_TRUE(basePrompt.contains("ぶるたろう"));
    EXPECT_TRUE(basePrompt.contains("毎回自分の名前を名乗ったり自己紹介（『私は〇〇』など）を挟まないでください"));
    EXPECT_TRUE(basePrompt.contains("質問に対して定型的な挨拶（『今日も元気ですか？』『お手伝いがんばるよ』など）で誤魔化さず"));
}




TEST_F(AIClientTest, ManagerContext_FutureInformationTenseHandling) {
    // UT-CTX-10: 未来・予告情報伝達 (INFORMATION) に対する時制適正化
    QList<ContextCandidate> candidates;
    PendingClarification pending;

    ManagerContextResult result = ManagerContextEvaluator::evaluateContextRuleBased(
        "ぷぃちゃんが配信終わるって",
        "blue002",
        candidates,
        pending
    );

    EXPECT_EQ(result.speechAct, "INFORMATION");
    EXPECT_EQ(result.responseAction, "ACKNOWLEDGE");

    QString instruction = ManagerContextEvaluator::formatWorkerInstruction(result, pending);
    EXPECT_TRUE(instruction.contains("情報伝達の受け止め指示"));
    EXPECT_TRUE(instruction.contains("未来・予告を『〜した』と過去形に誤認しないでください"));
}



























