#pragma once
#include <QObject>
#include <QList>
#include <QPair>
#include <QString>
#include "iai_client.h"
#include "../app_event.h"

class AIClientManager : public QObject {
    Q_OBJECT
private:
    IAIClient *m_currentClient = nullptr;
    QString m_apiKey;
    QString m_tavilyApiKey;
    QString m_provider; // "mistral" or "dummy"
    QString m_transCipherKey; // 難読化用の秘密鍵
    QList<QPair<QString, QString>> m_chatHistory; // 会話履歴 (ユーザー入力, AI応答)
    QString m_sessionContext; // マークダウンのコンテキスト要約
    bool m_isResetting = false; // 要約要求中かどうかのフラグ
    bool m_isManualReset = false; // 手動リセット中かどうかのフラグ
    QString m_lastPrompt; // 応答待ち中の最新プロンプト
    bool m_blacklistEnabled = true;
    QStringList m_blacklist;
    QStringList m_whitelist;
    bool m_isTranslationRequest = false;

    void loadCredentials();
    void loadSessionContext();
    void saveSessionContext(const QString &context);
    void saveObfuscatedLog(const QString &logText); // TransCipherを用いたログ難読化保存
    void loadBlacklist();
    void loadWhitelist();
    QString applyMask(const QString &text) const;
    bool isLanguageIndicator(const QString &lang) const;
    QString mapLanguage(const QString &lang) const;

public:
    explicit AIClientManager(QObject *parent = nullptr);
    ~AIClientManager();
    void setAIProvider(const QString &provider);

    // 履歴データ取得/設定用のI/F
    QList<QPair<QString, QString>> chatHistory() const { return m_chatHistory; }
    void setChatHistory(const QList<QPair<QString, QString>> &history) {
        m_chatHistory = history;
        emit chatHistoryUpdated(m_chatHistory);
    }

    // 暗号化バックアップの復号・読み出し用I/F
    QList<QPair<QString, QString>> loadObfuscatedBackup(const QString &filePath);

signals:
    void notifyEvent(const AppEvent &event);
    void chatHistoryUpdated(const QList<QPair<QString, QString>> &history); // 履歴更新シグナル

public slots:
    void on_requestAI(const QString &prompt);
    void on_clientRequestFinished(const QString &responseText, bool success);
    void resetSession(bool isManual); // セッションリセット機能
    bool importSessionBackup(const QString &filePath);
    void exportSessionBackup(const QString &encPath, const QString &txtPath);
    void on_settingsUpdated();
};
