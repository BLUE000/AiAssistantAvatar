#include <QApplication>
#include <QThread>
#include <QDebug>
#include "app_event.h"
#include "core_module.h"
#include "ui/avatar_window.h"
#include "ui/balloon_widget.h"
#include "twitch/twitch_reader.h"
#include "stt/stt_manager.h"
#include "ai/ai_client_manager.h"

// TrustChain のヘッダーインクルード
#include "TrustChainCore.hpp"
#include "TrustChainQt.hpp"

int main(int argc, char *argv[]) {
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

    // 5. 各種モジュールと常駐スレッドの生成
    QThread coreThread;
    QThread twitchThread;
    QThread sttThread;
    QThread aiThread;

    CoreModule *core = new CoreModule();
    TwitchReader *twitch = new TwitchReader();
    STTManager *stt = new STTManager();
    AIClientManager *ai = new AIClientManager();

    // 各オブジェクトを対応する常駐スレッドに移動
    core->moveToThread(&coreThread);
    twitch->moveToThread(&twitchThread);
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
    QObject::connect(&window, &AvatarWindow::requestChatHistory,
                     core, &CoreModule::on_requestChatHistory, Qt::QueuedConnection);

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
    QObject::connect(core, &CoreModule::requestChatHistory,
                     ai, &AIClientManager::on_requestChatHistory, Qt::QueuedConnection);

    // SubModules -> Core (イベント通知)
    QObject::connect(twitch, &TwitchReader::notifyEvent,
                     core, &CoreModule::on_notify_events, Qt::QueuedConnection);
    QObject::connect(stt, &STTManager::notifyEvent,
                     core, &CoreModule::on_notify_events, Qt::QueuedConnection);
    QObject::connect(ai, &AIClientManager::notifyEvent,
                     core, &CoreModule::on_notify_events, Qt::QueuedConnection);

    // 7. スレッドの開始
    coreThread.start();
    twitchThread.start();
    sttThread.start();
    aiThread.start();

    // Twitchコメント取得の自動開始要求
    emit core->requestTwitchStart();

    // 8. アプリケーション終了時の安全なクリーンアップ（常駐スレッドの解放）
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() {
        qDebug() << "Application quitting. Cleaning up threads...";

        // Twitchの停止
        twitch->on_stopReading();
        stt->on_stopListening();

        // 各スレッドに終了をシグナル
        coreThread.quit();
        twitchThread.quit();
        sttThread.quit();
        aiThread.quit();

        // 完全な終了をJOIN待機
        coreThread.wait();
        twitchThread.wait();
        sttThread.wait();
        aiThread.wait();

        // オブジェクトの破棄
        delete core;
        delete twitch;
        delete stt;
        delete ai;

        qDebug() << "All threads terminated safely. Cleanup finished.";
    });

    return app.exec();
}
