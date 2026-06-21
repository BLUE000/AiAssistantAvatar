#pragma once
#include <QObject>
#include "app_event.h"

class CoreModule : public QObject {
    Q_OBJECT
public:
    explicit CoreModule(QObject *parent = nullptr);
    ~CoreModule();

signals:
    // UIスレッドへ非同期で通知するシグナル
    void notifyEventToUI(const AppEvent &event);
    
    // サブモジュールへ要求を送るシグナル
    void requestTwitchStart();
    void requestSTTStart();
    void requestSTTStop();
    void requestAI(const QString &prompt);
    void requestSessionReset(bool isManual); // セッションリセット要求シグナル
    void requestSessionImport(const QString &filePath);
    void requestSessionExport(const QString &encPath, const QString &txtPath);
    void settingsUpdated(); // 設定更新通知シグナル
    void requestTwitchReauth(); // Twitch再認可要求シグナル

public slots:
    // 他モジュール（Twitch, STT, AI）からのイベントを受け取るスロット
    void on_notify_events(const AppEvent &event);

    // UIからの直接命令を受け取るスロット
    void on_startSTTRequested();
    void on_directInputSubmitted(const QString &text);
    void on_resetSessionRequested(); // UIからのセッションリセット要求を受け取るスロット
    void on_importSessionRequested(const QString &filePath);
    void on_exportSessionRequested(const QString &encPath, const QString &txtPath);
    void on_settingsUpdated(); // 設定更新を受け取るスロット
    void on_twitchReauthRequested(); // Twitch再認可要求を受け取るスロット
};
