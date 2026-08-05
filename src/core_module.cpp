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
            // TwitchからのメッセージはUIに中継（UIのキャーで順次処理）
            emit notifyEventToUI(event);
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
        case EventType::AIResponseReceived:
        case EventType::DirectInputSubmitted: {
            // AI応答受信時およびダイレクト入力時、送信元プラットフォームへ返信しつつUIにも表示
            if (event.extraData.contains("channel_id")) {
                QString channelId = event.extraData.value("channel_id").toString();
                qDebug() << "CoreModule: Routing response back to Discord. Channel:" << channelId;
                emit requestDiscordSend(channelId, event.text);
            } else if (event.extraData.contains("twitch_channel")) {
                QString twitchChannel = event.extraData.value("twitch_channel").toString();
                qDebug() << "CoreModule: Routing response back to Twitch. Channel:" << twitchChannel;
                emit requestTwitchSend(twitchChannel, event.text);
            }
            // Discord/Twitch/直接入力いずれの場合もUIに中継
            emit notifyEventToUI(event);
            break;
        }
        case EventType::TwitchConnectRequested: {
            // /twitch connect コマンド → TwitchReaderへ挨拶付き再接続を要求
            qDebug() << "CoreModule: Routing TwitchConnectRequested to TwitchReader.";
            emit requestTwitchConnect();
            // UIにもコマンド結果を表示
            emit notifyEventToUI(event);
            break;
        }
        case EventType::DiscordConnectRequested: {
            // /discord connect コマンド → DiscordReaderへ挨拶付き再接続を要求
            qDebug() << "CoreModule: Routing DiscordConnectRequested to DiscordReader.";
            emit requestDiscordConnect();
            // UIにもコマンド結果を表示
            emit notifyEventToUI(event);
            break;
        }
        case EventType::TwitchRaidReceived: {
            qDebug() << "CoreModule: Routing TwitchRaidReceived to AIClientManager. Raider:" << event.text;
            emit requestTwitchRaid(event.text);
            emit notifyEventToUI(event);
            break;
        }
        case EventType::ShoutoutSuccessReceived: {
            qDebug() << "CoreModule: Routing ShoutoutSuccessReceived to AIClientManager. Target:" << event.text;
            emit requestShoutoutSuccess(event.text);
            emit notifyEventToUI(event);
            break;
        }
        case EventType::VoiceInputCompleted: {
            qDebug() << "CoreModule: Voice input completed. Routing text to AI. Text:" << event.text;
            if (!event.text.trimmed().isEmpty()) {
                emit requestAI(event.text.trimmed(), "Streamer (Voice)");
            }
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

void CoreModule::on_stopSTTRequested() {
    qDebug() << "CoreModule: STT stop requested from UI.";
    emit requestSTTStop();
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
    qDebug() << "[TRACE-CORE] >>> CoreModule::on_settingsUpdated START";
    qDebug() << "CoreModule: Settings updated, propagating to submodules.";
    emit settingsUpdated();
    qDebug() << "[TRACE-CORE] <<< CoreModule::on_settingsUpdated END";
}

void CoreModule::on_twitchReauthRequested() {
    qDebug() << "[TRACE-CORE] >>> CoreModule::on_twitchReauthRequested START";
    qDebug() << "CoreModule: Twitch reauth requested, propagating to TwitchReader.";
    emit requestTwitchReauth();
    qDebug() << "[TRACE-CORE] <<< CoreModule::on_twitchReauthRequested END";
}

void CoreModule::on_deleteKnowledgeRequested(const QString &id) {
    qDebug() << "CoreModule: Propagation of deleteKnowledge to AIClientManager for ID:" << id;
    emit requestDeleteKnowledge(id);
}

void CoreModule::on_requestKnowledgeMetadata() {
    qDebug() << "CoreModule: Propagation of requestKnowledgeMetadata to AIClientManager";
    emit requestKnowledgeMetadata();
}


