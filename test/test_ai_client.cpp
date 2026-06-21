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
