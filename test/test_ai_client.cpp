#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include "ai/ai_client_manager.h"
#include "cipher_engine.h"

class AIClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        // テストのセットアップ時、古いlogディレクトリを削除してクリーンにする
        QDir("log").removeRecursively();
    }

    void TearDown() override {
        // テスト終了後、生成されたlogディレクトリをクリーンアップする
        QDir("log").removeRecursively();
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
    manager.on_clientRequestFinished("Response 1", true);

    EXPECT_EQ(historySpy.count(), 1);
    auto history = manager.chatHistory();
    EXPECT_EQ(history.size(), 1);
    EXPECT_EQ(history.last().first, "Hello 1");
    EXPECT_EQ(history.last().second, "Response 1");

    // 2. 履歴が5ペア（10メッセージ）に到達するまで対話を繰り返す
    for (int i = 2; i <= 4; ++i) {
        manager.on_requestAI(QString("Hello %1").arg(i));
        manager.on_clientRequestFinished(QString("Response %1").arg(i), true);
    }
    EXPECT_EQ(manager.chatHistory().size(), 4);

    // 5回目の対話 (これで自動リセット閾値の5ペアに到達)
    manager.on_requestAI("Hello 5");
    manager.on_clientRequestFinished("Response 5", true);

    // 自動リセット要求が走っているはずなので、要約応答をシミュレートする
    manager.on_clientRequestFinished("This is a summary markdown of the conversation.", true);

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
    manager.on_clientRequestFinished("# Summary\n- Q1 -> A1\n- Q2 -> A2", true);

    // 手動リセット時は UI バルーン通知用のイベントが発行されること
    EXPECT_GE(eventSpy.count(), 1);
    bool foundUIEvent = false;
    for (int i = 0; i < eventSpy.count(); ++i) {
        AppEvent event = eventSpy.at(i).at(0).value<AppEvent>();
        if (event.type == EventType::AIResponseReceived && event.text.contains("会話履歴をクリアし、コンテキスト要約を保存しました。")) {
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
    manager.on_clientRequestFinished("A5", true);

    // 要約応答をシミュレート
    manager.on_clientRequestFinished("# Summary", true);

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
    manager.on_clientRequestFinished("# Summary Test", true);

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
    manager.on_clientRequestFinished("# Summary Exp", true);

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
    
    QString configPath = "local_settings.json";
    #ifdef PROJECT_SOURCE_DIR
    {
        QString candidate = QString(PROJECT_SOURCE_DIR) + "/local_settings.json";
        if (QFile::exists(candidate)) {
            configPath = candidate;
        }
    }
    #endif

    QByteArray originalData;
    QFile file(configPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        originalData = file.readAll();
        file.close();
    }

    QJsonObject testObj;
    testObj["ai_provider"] = "dummy";
    testObj["mistral_api_key"] = "test_api_key_from_test";
    testObj["trans_cipher_key"] = "AiAssistantAvatar";
    testObj["twitch_channel"] = "YOUR_CHANNEL_NAME";
    testObj["twitch_client_id"] = "test_client_id";
    testObj["twitch_port"] = 48080;
    testObj["twitch_wakeword"] = "AI";
    testObj["twitch_wakeword_mode"] = "contains";
    
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(QJsonDocument(testObj).toJson());
        file.close();
    }

    // on_settingsUpdated() を呼び出して設定再読み込みが正常に動くか検証
    manager.on_settingsUpdated();

    // 元の設定に戻す
    if (!originalData.isEmpty()) {
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(originalData);
            file.close();
        }
    }

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

    // 既存 of blacklist.txt があれば退避
    QByteArray originalBlacklist;
    if (QFile::exists(blacklistPath)) {
        QFile file(blacklistPath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            originalBlacklist = file.readAll();
            file.close();
        }
        QFile::remove(blacklistPath);
    }

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

    // 既存の whitelist.txt があれば退避
    QByteArray originalWhitelist;
    if (QFile::exists(whitelistPath)) {
        QFile file(whitelistPath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            originalWhitelist = file.readAll();
            file.close();
        }
        QFile::remove(whitelistPath);
    }

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
    QString configPath = "local_settings.json";
#ifdef PROJECT_SOURCE_DIR
    {
        QString candidate = QString(PROJECT_SOURCE_DIR) + "/local_settings.json";
        if (QFile::exists(candidate)) {
            configPath = candidate;
        }
    }
#endif

    QByteArray originalConfig;
    {
        QFile file(configPath);
        if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            originalConfig = file.readAll();
            file.close();
        }
    }

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
    manager.on_clientRequestFinished("彼の言動はbakaであり、暴力はお勧めしません。", true);
    
    // 受信イベント内のテキストがマスクされているか検証
    ASSERT_GE(eventSpy.count(), 1);
    AppEvent receivedEvent = eventSpy.at(0).at(0).value<AppEvent>();
    EXPECT_EQ(receivedEvent.type, EventType::AIResponseReceived);
    EXPECT_EQ(receivedEvent.text, "彼の言動は****であり、****はお勧めしません。");

    // D. ホワイトリストの保護検証 (出力側)
    eventSpy.clear();
    manager.on_clientRequestFinished("私たちのclassでは、WTFと発言するのは禁止です。", true);
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
    manager.on_clientRequestFinished("これはbaka（ドラえもん）です。", true);
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

    // クリーニング：設定、ブラックリスト、ホワイトリストを元に戻す
    if (!originalConfig.isEmpty()) {
        QFile file(configPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(originalConfig);
            file.close();
        }
    } else {
        QFile::remove(configPath);
    }

    if (!originalBlacklist.isEmpty()) {
        QFile file(blacklistPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(originalBlacklist);
            file.close();
        }
    } else {
        QFile::remove(blacklistPath);
    }

    if (!originalWhitelist.isEmpty()) {
        QFile file(whitelistPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(originalWhitelist);
            file.close();
        }
    } else {
        QFile::remove(whitelistPath);
    }
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
    EXPECT_EQ(sentEvent.text, "trans en Hello World");

    // 完了シミュレート
    manager.on_clientRequestFinished("Hello World", true);

    // AIResponseReceived イベントを確認
    EXPECT_GE(eventSpy.count(), 2);
    AppEvent resEvent = eventSpy.at(1).at(0).value<AppEvent>();
    EXPECT_EQ(resEvent.type, EventType::AIResponseReceived);
    EXPECT_EQ(resEvent.text, "Hello World");

    // 履歴に追加されていないことを確認
    EXPECT_EQ(manager.chatHistory().size(), 0);
    EXPECT_EQ(historySpy.count(), 0);

    // 2. 言語省略の翻訳コマンドテスト ("trans こんにちは")
    eventSpy.clear();
    historySpy.clear();

    manager.on_requestAI("trans こんにちは");
    manager.on_clientRequestFinished("Hello", true);

    EXPECT_GE(eventSpy.count(), 2);
    EXPECT_EQ(eventSpy.at(0).at(0).value<AppEvent>().type, EventType::AIRequestSent);
    EXPECT_EQ(eventSpy.at(1).at(0).value<AppEvent>().type, EventType::AIResponseReceived);
    EXPECT_EQ(eventSpy.at(1).at(0).value<AppEvent>().text, "Hello");

    // 履歴に追加されていないこと
    EXPECT_EQ(manager.chatHistory().size(), 0);
    EXPECT_EQ(historySpy.count(), 0);
}
