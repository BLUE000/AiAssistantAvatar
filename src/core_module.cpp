#include "core_module.h"
#include <QDebug>

CoreModule::CoreModule(QObject *parent) : QObject(parent) {
    qDebug() << "CoreModule initialized.";
}

CoreModule::~CoreModule() {
    qDebug() << "CoreModule destroyed.";
}

void CoreModule::on_notify_events(const AppEvent &event) {
    qDebug() << "CoreModule received event from" << event.source << "Type:" << static_cast<int>(event.type);
    
    // イベント種別ごとの中継ロジック
    switch (event.type) {
        case EventType::TwitchCommentReceived:
        case EventType::VoiceInputCompleted: {
            // AI要求を発行し、同時にUIへ送信中イベントを通知する
            AppEvent sentEvent;
            sentEvent.type = EventType::AIRequestSent;
            sentEvent.source = "CoreModule";
            sentEvent.text = event.text;
            emit notifyEventToUI(sentEvent);

            emit requestAI(event.text);
            break;
        }
        case EventType::AIResponseReceived: {
            // そのままUIにパス
            emit notifyEventToUI(event);
            break;
        }
        default:
            // その他はUIに通知中継
            emit notifyEventToUI(event);
            break;
    }
}

void CoreModule::on_startSTTRequested() {
    qDebug() << "CoreModule: STT start requested from UI.";
    emit requestSTTStart();
}

void CoreModule::on_directInputSubmitted(const QString &text) {
    qDebug() << "CoreModule: Direct text input submitted:" << text;
    
    // UIへ入力完了を即時通知
    AppEvent uiEvent;
    uiEvent.type = EventType::DirectInputSubmitted;
    uiEvent.source = "CoreModule";
    uiEvent.text = text;
    emit notifyEventToUI(uiEvent);

    // AIリクエスト要求シグナルを発火
    AppEvent sentEvent;
    sentEvent.type = EventType::AIRequestSent;
    sentEvent.source = "CoreModule";
    sentEvent.text = text;
    emit notifyEventToUI(sentEvent);

    emit requestAI(text);
}

void CoreModule::on_resetSessionRequested() {
    qDebug() << "CoreModule: Session reset requested from UI.";
    emit requestSessionReset(true); // 手動リセット
}

void CoreModule::on_importSessionRequested(const QString &filePath) {
    qDebug() << "CoreModule: Session import requested from UI. File:" << filePath;
    emit requestSessionImport(filePath);
}

void CoreModule::on_exportSessionRequested(const QString &encPath, const QString &txtPath) {
    qDebug() << "CoreModule: Session export requested from UI. Enc:" << encPath << "Txt:" << txtPath;
    emit requestSessionExport(encPath, txtPath);
}

