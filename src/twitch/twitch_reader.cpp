#include "twitch_reader.h"
#include <QDebug>
#include <QTimer>

TwitchReader::TwitchReader(QObject *parent) 
    : QObject(parent), m_wakeWord("アバターさん") 
{
}

TwitchReader::~TwitchReader() {
}

void TwitchReader::setSettings(const QString &channel, const QString &token, const QString &clientId, const QString &wakeWord) {
    m_channel = channel;
    m_oauthToken = token;
    m_clientId = clientId;
    m_wakeWord = wakeWord;
    qDebug() << "TwitchReader: Settings updated. Channel:" << channel << "WakeWord:" << wakeWord;
}

void TwitchReader::on_startReading() {
    if (m_isRunning) return;
    m_isRunning = true;
    qDebug() << "TwitchReader: Thread started. Connecting to Twitch IRC over WebSocket...";
    
    // ※実動作時はここにWebSocket/IRC接続の非同期スレッド処理が入る
    // 今回は初期ダミーとして、起動中の確認ログを出力
}

void TwitchReader::on_stopReading() {
    if (!m_isRunning) return;
    m_isRunning = false;
    qDebug() << "TwitchReader: Disconnecting from Twitch IRC...";
}

void TwitchReader::injectTestComment(const QString &user, const QString &message) {
    qDebug() << "TwitchReader: Injected comment from" << user << ":" << message;

    // ウェイクワード（トリガーメッセージ）のチェック
    if (!m_wakeWord.isEmpty() && message.contains(m_wakeWord)) {
        // コメント内容からウェイクワードの部分を除去して命令を取り出す
        QString cleanMessage = message;
        cleanMessage.replace(m_wakeWord, "");
        cleanMessage = cleanMessage.trimmed();

        AppEvent event;
        event.type = EventType::TwitchCommentReceived;
        event.source = "TwitchReader";
        event.text = cleanMessage;
        
        QVariantMap meta;
        meta["user"] = user;
        meta["raw_message"] = message;
        event.extraData = meta;

        emit notifyEvent(event);
    }
}
