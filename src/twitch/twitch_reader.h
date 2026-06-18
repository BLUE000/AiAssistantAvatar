#pragma once
#include <QObject>
#include "../app_event.h"

class TwitchReader : public QObject {
    Q_OBJECT
private:
    bool m_isRunning = false;
    QString m_channel;
    QString m_oauthToken;
    QString m_clientId;
    QString m_wakeWord; // UIで設定変更可能

public:
    explicit TwitchReader(QObject *parent = nullptr);
    ~TwitchReader();

    void setSettings(const QString &channel, const QString &token, const QString &clientId, const QString &wakeWord);

signals:
    void notifyEvent(const AppEvent &event);

public slots:
    void on_startReading();
    void on_stopReading();
    
    // テスト用の擬似コメント注入用スロット
    void injectTestComment(const QString &user, const QString &message);
};
