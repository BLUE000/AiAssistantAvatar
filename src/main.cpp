#include <QApplication>
#include <QThread>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QMutex>
#include "app_event.h"
#include "core_module.h"
#include "ui/avatar_window.h"
#include "twitch/twitch_reader.h"
#include "stt/stt_manager.h"
#include "ai/ai_client_manager.h"
#include "discord/discord_reader.h"
#include "obs/obs_http_server.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>

// TrustChain のヘッダーインクルード
#include "TrustChainCore.hpp"
#include "TrustChainQt.hpp"

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    static QMutex mutex;
    QMutexLocker locker(&mutex);
    
    QFile file("app_debug.log");
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream stream(&file);
        QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
        QString typeStr = "DEBUG";
        switch (type) {
            case QtDebugMsg: typeStr = "DEBUG"; break;
            case QtInfoMsg: typeStr = "INFO"; break;
            case QtWarningMsg: typeStr = "WARN"; break;
            case QtCriticalMsg: typeStr = "CRIT"; break;
            case QtFatalMsg: typeStr = "FATAL"; break;
        }
        stream << QString("[%1] [%2] %3\n").arg(timeStr).arg(typeStr).arg(msg);
        file.close();
    }
    
    // 標準出力にも出力
    fprintf(stderr, "%s\n", msg.toLocal8Bit().constData());
    fflush(stderr);
}

int main(int argc, char *argv[]) {
    qInstallMessageHandler(messageHandler);
    QApplication app(argc, argv);

    // 1. メタタイプの登録 (スレッド間で AppEvent を渡すために必須)
    qRegisterMetaType<AppEvent>("AppEvent");

    // 2. TrustChain による改ざん検知の実行
    qDebug() << "TrustChain: Starting provenance verification...";
    TrustChain::Core guard;
    TrustChain::AuthStatus status = guard.verifyToken();

    // 3. アプリメインUI (AvatarWindow) の作成
    AvatarWindow window;

    // 4. TrustChain ウォーターマークの適用
    // 改造ビルドやライセンス状況に応じて、タイトルやウォーターマークを描画する
    TrustChain::QtHelper::applyWatermark(&window, status);

    window.show();

    // OBS用簡易HTTPサーバーの初期化
    ObsHttpServer *httpServer = new ObsHttpServer();
    QString settingsPath = QCoreApplication::applicationDirPath() + "/local_settings.json";
    if (!QFile::exists(settingsPath)) {
        settingsPath = "local_settings.json";
    }
#ifdef PROJECT_SOURCE_DIR
    if (!QFile::exists(settingsPath)) {
        settingsPath = QString(PROJECT_SOURCE_DIR) + "/local_settings.json";
    }
#endif

    auto loadAndStartHttpServer = [httpServer, settingsPath]() {
        bool enabled = false;
        quint16 port = 58082;
        QFile file(settingsPath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
            file.close();
            enabled = obj.value("obs_http_enabled").toBool(false);
            port = obj.value("obs_http_port").toInt(58082);
        }
        if (enabled) {
            httpServer->start(port);
        } else {
            httpServer->stop();
        }
    };

    // 初期起動
    loadAndStartHttpServer();

    // 設定変更時の再ロード
    QObject::connect(&window, &AvatarWindow::settingsUpdated, [loadAndStartHttpServer]() {
        loadAndStartHttpServer();
    });

    // 5. 各種モジュールと常駐スレッドの生成
    QThread coreThread;
    QThread twitchThread;
    QThread discordThread;
    QThread sttThread;
    QThread aiThread;

    CoreModule *core = new CoreModule();
    TwitchReader *twitch = new TwitchReader();
    DiscordReader *discord = new DiscordReader();
    STTManager *stt = new STTManager();
    AIClientManager *ai = new AIClientManager();

    // 各オブジェクトを対応する常駐スレッドに移動
    core->moveToThread(&coreThread);
    twitch->moveToThread(&twitchThread);
    discord->moveToThread(&discordThread);
    stt->moveToThread(&sttThread);
    ai->moveToThread(&aiThread);

    // 6. スレッド間シグナル・スロットの接続 (QueuedConnection)

    // UI -> Core
    QObject::connect(&window, &AvatarWindow::startSTTRequested,
                     core, &CoreModule::on_startSTTRequested, Qt::QueuedConnection);
    QObject::connect(&window, &AvatarWindow::directInputSubmitted,
                     core, &CoreModule::on_directInputSubmitted, Qt::QueuedConnection);
    QObject::connect(&window, &AvatarWindow::resetSessionRequested,
                     core, &CoreModule::on_resetSessionRequested, Qt::QueuedConnection);
    QObject::connect(&window, &AvatarWindow::importSessionRequested,
                     core, &CoreModule::on_importSessionRequested, Qt::QueuedConnection);
    QObject::connect(&window, &AvatarWindow::exportSessionRequested,
                     core, &CoreModule::on_exportSessionRequested, Qt::QueuedConnection);
    QObject::connect(&window, &AvatarWindow::settingsUpdated,
                     core, &CoreModule::on_settingsUpdated, Qt::QueuedConnection);
    QObject::connect(&window, &AvatarWindow::twitchReauthRequested,
                     core, &CoreModule::on_twitchReauthRequested, Qt::QueuedConnection);

    // UI -> AI (Direct Execution)
    QObject::connect(&window, &AvatarWindow::requestAIExecution,
                     ai, &AIClientManager::on_requestAI, Qt::QueuedConnection);

    // Core -> UI
    QObject::connect(core, &CoreModule::notifyEventToUI,
                     &window, &AvatarWindow::on_notify_events, Qt::QueuedConnection);

    // Core -> SubModules (要求)
    QObject::connect(core, &CoreModule::requestTwitchStart,
                     twitch, &TwitchReader::on_startReading, Qt::QueuedConnection);
    QObject::connect(core, &CoreModule::requestSTTStart,
                     stt, &STTManager::on_startListening, Qt::QueuedConnection);
    QObject::connect(core, &CoreModule::requestSTTStop,
                     stt, &STTManager::on_stopListening, Qt::QueuedConnection);
    QObject::connect(core, &CoreModule::requestAI,
                     ai, &AIClientManager::on_requestAI, Qt::QueuedConnection);
    QObject::connect(core, &CoreModule::requestSessionReset,
                     ai, &AIClientManager::resetSession, Qt::QueuedConnection);
    QObject::connect(core, &CoreModule::requestSessionImport,
                     ai, &AIClientManager::importSessionBackup, Qt::QueuedConnection);
    QObject::connect(core, &CoreModule::requestSessionExport,
                     ai, &AIClientManager::exportSessionBackup, Qt::QueuedConnection);
    QObject::connect(core, &CoreModule::settingsUpdated,
                     ai, &AIClientManager::on_settingsUpdated, Qt::QueuedConnection);
    QObject::connect(core, &CoreModule::settingsUpdated,
                     twitch, &TwitchReader::on_settingsUpdated, Qt::QueuedConnection);
    QObject::connect(core, &CoreModule::settingsUpdated,
                     discord, &DiscordReader::on_settingsUpdated, Qt::QueuedConnection);
    QObject::connect(core, &CoreModule::requestTwitchReauth,
                     twitch, &TwitchReader::on_twitchReauthRequested, Qt::QueuedConnection);
    QObject::connect(core, &CoreModule::requestDiscordSend,
                     discord, &DiscordReader::on_requestDiscordSend, Qt::QueuedConnection);
    QObject::connect(core, &CoreModule::requestTwitchSend,
                     twitch, &TwitchReader::on_requestTwitchSend, Qt::QueuedConnection);
    // /twitch connect / /discord connect コマンド → 挨拶付き再接続
    QObject::connect(core, &CoreModule::requestTwitchConnect,
                     twitch, &TwitchReader::on_twitchConnectRequested, Qt::QueuedConnection);
    QObject::connect(core, &CoreModule::requestDiscordConnect,
                     discord, &DiscordReader::on_discordConnectRequested, Qt::QueuedConnection);

    // SubModules -> Core (イベント通知)
    QObject::connect(twitch, &TwitchReader::notifyEvent,
                     core, &CoreModule::on_notify_events, Qt::QueuedConnection);
    QObject::connect(discord, &DiscordReader::notifyEvent,
                     core, &CoreModule::on_notify_events, Qt::QueuedConnection);
    QObject::connect(stt, &STTManager::notifyEvent,
                     core, &CoreModule::on_notify_events, Qt::QueuedConnection);
    QObject::connect(ai, &AIClientManager::notifyEvent,
                     core, &CoreModule::on_notify_events, Qt::QueuedConnection);

    // AI -> UI (ニックネームデータ同期)
    QObject::connect(ai, &AIClientManager::userNamesUpdated,
                     &window, &AvatarWindow::onNicknameDataUpdated, Qt::QueuedConnection);

    // UI -> AI (ニックネーム操作要求)
    QObject::connect(&window, &AvatarWindow::approveNicknameRequested,
                     ai, &AIClientManager::approveNicknameRequest, Qt::QueuedConnection);
    QObject::connect(&window, &AvatarWindow::rejectNicknameRequested,
                     ai, &AIClientManager::rejectNicknameRequest, Qt::QueuedConnection);
    QObject::connect(&window, &AvatarWindow::deleteNicknameRequested,
                     ai, &AIClientManager::deleteNickname, Qt::QueuedConnection);
    QObject::connect(&window, &AvatarWindow::updateNicknamePreferredRequested,
                     ai, &AIClientManager::updateNicknamePreferred, Qt::QueuedConnection);

    // UI -> Core (ナレッジ管理要求)
    QObject::connect(&window, &AvatarWindow::deleteKnowledgeRequested,
                     core, &CoreModule::on_deleteKnowledgeRequested, Qt::QueuedConnection);
    QObject::connect(&window, &AvatarWindow::requestKnowledgeMetadataRequested,
                     core, &CoreModule::on_requestKnowledgeMetadata, Qt::QueuedConnection);

    // Core -> AI (ナレッジ管理要求中継)
    QObject::connect(core, &CoreModule::requestDeleteKnowledge,
                     ai, &AIClientManager::deleteKnowledge, Qt::QueuedConnection);
    QObject::connect(core, &CoreModule::requestKnowledgeMetadata,
                     ai, &AIClientManager::on_requestKnowledgeMetadata, Qt::QueuedConnection);

    // AI -> UI (ナレッジデータ同期)
    QObject::connect(ai, &AIClientManager::knowledgeMetadataUpdated,
                     &window, &AvatarWindow::onKnowledgeDataUpdated, Qt::QueuedConnection);

    // 7. スレッドの開始
    coreThread.start();
    twitchThread.start();
    discordThread.start();
    sttThread.start();
    aiThread.start();

    // Twitchコメント取得の自動開始要求
    emit core->requestTwitchStart();
    QMetaObject::invokeMethod(discord, "on_startReading", Qt::QueuedConnection);

    // 起動時の初期データ送信要求
    QMetaObject::invokeMethod(ai, "saveUserNames", Qt::QueuedConnection);
    QMetaObject::invokeMethod(ai, "on_requestKnowledgeMetadata", Qt::QueuedConnection);

    // 8. アプリケーション終了時の安全なクリーンアップ（常駐スレッドの解放）
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() {
        qDebug() << "Application quitting. Cleaning up threads...";

        // 各スレッドのスロットを適切なスレッドコンテキストで実行させるため、invokeMethodを使用する
        QMetaObject::invokeMethod(twitch, "on_stopReading", Qt::QueuedConnection);
        QMetaObject::invokeMethod(discord, "on_stopReading", Qt::QueuedConnection);
        QMetaObject::invokeMethod(stt, "on_stopListening", Qt::QueuedConnection);

        // 各スレッドに終了をシグナル
        coreThread.quit();
        twitchThread.quit();
        discordThread.quit();
        sttThread.quit();
        aiThread.quit();

        // 完全な終了をJOIN待機
        coreThread.wait();
        twitchThread.wait();
        discordThread.wait();
        sttThread.wait();
        aiThread.wait();

        // オブジェクトの破棄
        httpServer->stop();
        delete httpServer;

        delete core;
        delete twitch;
        delete discord;
        delete stt;
        delete ai;

        qDebug() << "All threads terminated safely. Cleanup finished.";
    });

    return app.exec();
}
