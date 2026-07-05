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
    
    switch (event.type) {
        case EventType::TwitchCommentReceived: {
            // TwitchからのメッセージはUIに中継しつつ、AIにもリクエストする
            QString username = event.extraData.value("user").toString();
            QString twitchChannel = event.extraData.value("twitch_channel").toString();
            // userパラメータに [Twitch:channel] username 形式で埋め込む
            QString encodedUser = twitchChannel.isEmpty()
                ? QString("[Twitch] %1").arg(username)
                : QString("[Twitch:%1] %2").arg(twitchChannel, username);

            qDebug() << "CoreModule: Routing Twitch message to AI. User:" << encodedUser;
            emit notifyEventToUI(event); // UIにも表示
            emit requestAI(event.text, encodedUser);
            break;
        }
        case EventType::DiscordMessageReceived: {
            // DiscordからのメッセージはUIに中継せず、直接AIにリクエストする (アバター非連動)
            QString channelId = event.extraData.value("channel_id").toString();
            QString username = event.extraData.value("username").toString();
            // userパラメータに [Discord:channelId] username 形式で埋め込む
            QString encodedUser = QString("[Discord:%1] %2").arg(channelId, username);
            
            qDebug() << "CoreModule: Routing Discord message to AI. User:" << encodedUser;
            emit requestAI(event.text, encodedUser);
            break;
        }
        case EventType::AIResponseReceived: {
            // AI応答受信時、送信元プラットフォームへ返信しつつUIにも表示
            if (event.extraData.contains("channel_id")) {
                QString channelId = event.extraData.value("channel_id").toString();
                qDebug() << "CoreModule: Routing AI response back to Discord. Channel:" << channelId;
                emit requestDiscordSend(channelId, event.text);
            } else if (event.extraData.contains("twitch_channel")) {
                QString twitchChannel = event.extraData.value("twitch_channel").toString();
                qDebug() << "CoreModule: Routing AI response back to Twitch. Channel:" << twitchChannel;
                emit requestTwitchSend(twitchChannel, event.text);
            }
            // Discord/Twitch/直接入力いずれの場合もUIに中継
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

    emit requestAI(text, "");
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

void CoreModule::on_settingsUpdated() {
    qDebug() << "CoreModule: Settings updated, propagating to submodules.";
    emit settingsUpdated();
}

void CoreModule::on_twitchReauthRequested() {
    qDebug() << "CoreModule: Twitch reauth requested, propagating to TwitchReader.";
    emit requestTwitchReauth();
}


