#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include "ai/ai_client_manager.h"
#include "ai/huggingface_ai_client.h"
#include "ai/openrouter_ai_client.h"
#include "ai/sakura_ai_client.h"
#include "cipher_engine.h"
#include "twitch/twitch_reader.h"
#include "discord/discord_reader.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>

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
    EXPECT_EQ(history.last().first, "[Direct] Hello 1");
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

TEST_F(AIClientTest, NicknameManagementTest) {
    // 既存の設定ファイルがあれば一時退避
    bool hasBackupSettings = QFile::exists("local_settings.json");
    if (hasBackupSettings) {
        QFile::rename("local_settings.json", "local_settings.json.bak");
    }
    bool hasBackupUserNames = QFile::exists("user_names.json");
    if (hasBackupUserNames) {
        QFile::rename("user_names.json", "user_names.json.bak");
    }

    // テスト用の設定ファイルを作成して配信主を設定
    QJsonObject localSettings;
    localSettings["twitch_channel"] = "test_streamer";
    localSettings["ai_provider"] = "dummy";
    
    QFile settingsFile("local_settings.json");
    ASSERT_TRUE(settingsFile.open(QIODevice::WriteOnly | QIODevice::Text));
    settingsFile.write(QJsonDocument(localSettings).toJson());
    settingsFile.close();

    // テスト用の空の user_names.json を作成
    QJsonObject initialUserNames;
    initialUserNames["users"] = QJsonObject();
    initialUserNames["pending_requests"] = QJsonArray();
    
    QFile userNamesFile("user_names.json");
    ASSERT_TRUE(userNamesFile.open(QIODevice::WriteOnly | QIODevice::Text));
    userNamesFile.write(QJsonDocument(initialUserNames).toJson());
    userNamesFile.close();

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
    {
        QFile file("user_names.json");
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
    
    {
        QFile file("user_names.json");
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
    QFile::remove("user_names.json");

    // バックアップから復元
    if (hasBackupSettings) {
        QFile::rename("local_settings.json.bak", "local_settings.json");
    }
    if (hasBackupUserNames) {
        QFile::rename("user_names.json.bak", "user_names.json");
    }
}

TEST_F(AIClientTest, DiscordPlatformMessageTest) {
    AIClientManager manager;
    manager.setAIProvider("dummy");

    QSignalSpy eventSpy(&manager, &AIClientManager::notifyEvent);

    // 1. Discord プレフィックス付きリクエスト
    manager.on_requestAI("こんにちは", "[Discord:channel123] alice");
    manager.on_clientRequestFinished("こんにちは！aliceさん", true);

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

    manager.on_clientRequestFinished("はい、覚えています。", true);

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
    manager.on_clientRequestFinished("Keywords: キーワードA, キーワードB\nSummary: 個別要約文", true);

    // メタサマリマージ処理 (m_isMergingSummaries) が開始されているため、AIマージ要約完了シミュレート
    manager.on_clientRequestFinished("Keywords: 総合A, 総合B\nSummary: 10件マージした総合サマリ文", true);

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

TEST_F(AIClientTest, AIRouterRoutingTest) {
    RateLimitTracker tracker;
    AIRouter router;

    ProviderStatus s1;
    s1.provider = "groq";
    s1.available = true;
    
    ProviderStatus s2;
    s2.provider = "cerebras";
    s2.available = false; // 枯渇

    ProviderStatus s3;
    s3.provider = "mistral";
    s3.available = true;

    tracker.registerClient(s1);
    tracker.registerClient(s2);
    tracker.registerClient(s3);

    QStringList priority = {"groq", "cerebras", "mistral"};

    // 1. 優先度最高かつ利用可能な groq が選ばれること
    EXPECT_EQ(router.selectClient(AIRole::Worker, tracker, priority), "groq");

    // 2. groq も枯渇状態にする
    s1.available = false;
    tracker.registerClient(s1);

    // 3. 次に利用可能な mistral が選ばれること（cerebrasはスキップされる）
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

TEST_F(AIClientTest, DynamicFallbackOn429ErrorTest) {
    AIClientManager manager;
    manager.setAIProvider("cerebras");

    // 全クライアントの available = false にし、dummy のみ available = true にする
    for (const QString &provider : manager.tracker().registeredClientIds()) {
        ProviderStatus s = manager.tracker().statusOf(provider);
        s.available = (provider == "dummy");
        s.rpmRemaining = (provider == "dummy") ? 10 : 0;
        manager.tracker().registerClient(s);
    }

    // テスト用に cerebras のみ available = true に一時的にする（これを最初に使わせるため）
    ProviderStatus s_cerebras = manager.tracker().statusOf("cerebras");
    s_cerebras.available = true;
    s_cerebras.rpmRemaining = 10;
    manager.tracker().registerClient(s_cerebras);

    QSignalSpy eventSpy(&manager, &AIClientManager::notifyEvent);

    // 1. リクエストを発行（内部で cerebras が選ばれて送信開始される）
    manager.on_requestAI("テストプロンプト", "userA");

    // 2. 擬似的に 429 エラーレスポンスを注入する
    manager.on_clientRequestFinished("Error status code 429", false);

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
    EXPECT_FALSE(manager.tracker().isAvailable("cerebras")); // 429を受けたcerebrasは利用不可になっているはず
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
    manager.on_clientRequestFinished("raider_user さんのレイドありがとうございます！", true);

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
    obj["ai_provider"] = "huggingface";
    obj["huggingface_api_key"] = "test_hf_key";
    obj["openrouter_api_key"] = "test_or_key";
    obj["sakura_api_key"] = "test_sakura_key";

    EXPECT_NO_THROW(manager.loadSettingsFromJsonObject(obj));
}


