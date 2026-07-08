#pragma once
#include <QObject>
#include <QList>
#include <QPair>
#include <QString>
#include <QJsonObject>
#include <QTimer>
#include "iai_client.h"
#include "../app_event.h"

enum class KnowledgeImportState {
    Idle,
    AwaitingFileAndExplanation,
    CancelConfirmation,
    QandAMode
};

class AIClientManager : public QObject {
    Q_OBJECT
private:
    IAIClient *m_currentClient = nullptr;
    QString m_apiKey;
    QString m_tavilyApiKey;
    QString m_cerebrasApiKey;
    QString m_cerebrasModel;
    QString m_provider; // "mistral", "cerebras", or "dummy"
    QString m_transCipherKey; // 難読化用の秘密鍵
    QList<QPair<QString, QString>> m_chatHistory; // 会話履歴 (ユーザー入力, AI応答)
    QString m_sessionContext; // マークダウンのコンテキスト要約
    bool m_isResetting = false; // 要約要求中かどうかのフラグ
    bool m_isManualReset = false; // 手動リセット中かどうかのフラグ
    QString m_lastPrompt; // 応答待ち中の最新プロンプト
    QString m_lastPromptWithTag; // 送信元タグ付きの最新プロンプト
    QString m_lastFinalPrompt; // 最終的に送信されたシステム指示/RAG入りプロンプト
    QString m_currentResetSessionId;
    QString m_currentResetStartTime;
    bool m_isMergingSummaries = false;
    QStringList m_mergingSessionIds;
    bool m_blacklistEnabled = true;
    QStringList m_blacklist;
    QStringList m_whitelist;
    bool m_isTranslationRequest = false;
    QString m_streamerName;
    QString m_avatarName;
    QString m_currentRequester;
    QString m_previousRequester; // 前回のリクエスター（切り替わり検知用）

    struct PendingRequest {
        QString prompt;
        QString user;
    };
    QList<PendingRequest> m_pendingRequests;
    void processPendingRequests();

    void loadCredentials();
    void loadSessionContext();
    void saveSessionContext(const QString &context);
    void saveObfuscatedLog(const QString &logText); // TransCipherを用いたログ難読化保存
    void loadBlacklist();
    void loadWhitelist();
    void loadUserNames();
    QString applyMask(const QString &text) const;

    // Discord / Twitch / 長期記憶用メンバ
    QString m_currentDiscordChannelId;
    QString m_currentTwitchChannel; // Twitch入力時の返信先チャンネル名
    QString m_recalledContext;
    void scanMemorySummaries(const QString &prompt);
    void loadMemoryDetail(const QString &sessionId);
    void checkAndMergeSummaries();

    QJsonObject m_userNamesObj;
    bool isLanguageIndicator(const QString &lang) const;
    QString mapLanguage(const QString &lang) const;

    // ナレッジ管理・対話登録メンバ
    KnowledgeImportState m_importState = KnowledgeImportState::Idle;
    QTimer *m_importTimeoutTimer = nullptr;
    QString m_importingFileName;
    QString m_importingFileContent;
    QJsonObject m_knowledgeMetadata;

    void loadKnowledgeMetadata();
    void saveKnowledgeMetadata();
    void scanStaticKnowledge(const QString &prompt, QString &recalledPrompt);

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

    KnowledgeImportState importState() const { return m_importState; }
    QJsonObject knowledgeMetadata() const { return m_knowledgeMetadata; }
    QString lastFinalPrompt() const { return m_lastFinalPrompt; }
    QString avatarName() const { return m_avatarName; }

    // 暗号化バックアップの復号・読み出し用I/F
    QList<QPair<QString, QString>> loadObfuscatedBackup(const QString &filePath);

signals:
    void notifyEvent(const AppEvent &event);
    void chatHistoryUpdated(const QList<QPair<QString, QString>> &history); // 履歴更新シグナル
    void userNamesUpdated(const QJsonObject &data);
    void knowledgeMetadataUpdated(const QJsonObject &data);

public slots:
    void on_requestAI(const QString &prompt, const QString &user = "");
    void on_clientRequestFinished(const QString &responseText, bool success);
    void resetSession(bool isManual); // セッションリセット機能
    bool importSessionBackup(const QString &filePath);
    void exportSessionBackup(const QString &encPath, const QString &txtPath);
    void on_settingsUpdated();

    // ニックネーム管理用
    QString handleNicknameUpdateRequest(const QString &target, const QString &nickname);
    void saveUserNames();
    void approveNicknameRequest(const QString &requester, const QString &target, const QString &nickname);
    void rejectNicknameRequest(const QString &requester, const QString &target, const QString &nickname);
    void deleteNickname(const QString &user);
    void updateNicknamePreferred(const QString &user, const QString &preferred);

    // ナレッジ管理スロット
    void deleteKnowledge(const QString &id);
    void on_requestKnowledgeMetadata();
    void onImportTimeout();
    QString finalizeKnowledgeImport(const QString &title, const QString &description, const QStringList &keywords);
    void openKnowledgeInputFolder();

    QJsonObject userNamesObj() const { return m_userNamesObj; }
};
