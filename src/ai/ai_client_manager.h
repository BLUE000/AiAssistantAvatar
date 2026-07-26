#pragma once
#include <QObject>
#include <QList>
#include <QPair>
#include <QString>
#include <QJsonObject>
#include <QTimer>
#include "iai_client.h"
#include "../app_event.h"
#include "rate_limit_tracker.h"
#include "ai_router.h"
#include "../search/markdown_table_engine.h"

enum class KnowledgeImportState {
    Idle,
    AwaitingFileAndExplanation,
    CancelConfirmation,
    QandAMode
};

struct ConversationEntry {
    QString timestamp;
    QString sender;
    QString text;
    bool isSummarized = false;
};

class SystemResponseManager;

class AIClientManager : public QObject {
    Q_OBJECT
public:
    void loadCredentials();
private:
    IAIClient *m_currentClient = nullptr;
    SystemResponseManager *m_systemResponseManager = nullptr;
    QString m_provider; // 現在のWorker API（UI設定、優先度の最優先に配置）
    QString m_transCipherKey; // 難読化用の秘密鍵
    QList<QPair<QString, QString>> m_chatHistory; // 会話履歴 (ユーザー入力, AI応答)
    QString m_sessionContext; // マークダウンのコンテキスト要約
    bool m_isResetting = false; // 要約要求中かどうかのフラグ
    bool m_isManualReset = false; // 手動リセット中かどうかのフラグ
    QString m_lastPrompt; // 応答待ち中の最新プロンプト
    QString m_lastPromptWithTag; // 送信元タグ付きの最新プロンプト
    QString m_lastFinalPrompt; // 最終的に送信されたシステム指示/RAG入りプロンプト
    QString m_lastAdditionalSystemPrompt; // 最終的に送信されたシステム指示(RAG等)
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

    // --- F-15/F-16/F-32 AIプロバイダ追加 ---
    class MistralAIClient *m_mistralClient = nullptr;
    class CerebrasAIClient *m_cerebrasClient = nullptr;
    class GroqAIClient *m_groqClient = nullptr;
    class HuggingFaceAIClient *m_huggingfaceClient = nullptr;
    class OpenRouterAIClient *m_openrouterClient = nullptr;
    class SakuraAIClient *m_sakuraClient = nullptr;
    class DummyAIClient *m_dummyClient = nullptr;

    QMap<QString, IAIClient*> m_clientMap;
    RateLimitTracker m_tracker;
    AIRouter m_router;
    MarkdownTableEngine m_tableEngine;

    bool m_managerEnabled = false;
    QString m_managerProvider = "groq";
    QString m_managerModel = "llama-3.1-8b-instant";
    QString m_groqModel = "llama-3.3-70b-versatile";
    QString m_cerebrasModel = "llama3.1-8b";
    QString m_mistralModel = "mistral-small-latest";
    QString m_huggingfaceModel = "meta-llama/Llama-3.1-8B-Instruct";
    QString m_openrouterModel = "meta-llama/llama-3.1-8b-instruct:free";
    QString m_sakuraModel = "sakura-llm";
    QString m_tavilyApiKey;
    bool m_taskFlowEnabled = true;
    QString m_taskFlowApiUrl;

    // TaskFlow外部スケジュールAPI連携RAG
    QString fetchSchedules(const QString &category, const class QDate &startDate, int days);
    QString getTaskFlowSchedulesContext();
    qint64 m_apiCallStartTimeMs = 0;

    QStringList workerPriorityOrder() const;
    QStringList managerPriorityOrder() const;

    struct PendingRequest {
        QString prompt;
        QString user;
    };
    QList<PendingRequest> m_pendingRequests;
    void processPendingRequests();
    bool selectAndPrepareClient();

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



    // --- F-22 レイド・クリエイター自動紹介メンバ ---
    struct PendingShoutout {
        QString username;
        QString displayName;
        QDateTime requestTime;
    };

    class TwitchHelixClient *m_helixClient = nullptr;
    bool m_raidAutoShoutoutEnabled = true;
    bool m_shoutoutConversationEnabled = true;
    bool m_shoutoutUseCommand = true;
    bool m_shoutoutFollowMsgEnabled = true;
    QString m_shoutoutFollowMsgTemplate = "ぜひ {name} さんをフォローしてね！";
    bool m_shoutoutUseAnnounce = true;
    QString m_shoutoutAnnounceColor = "random";
    QString m_shoutoutLength = "standard";
    QString m_shoutoutTone = "明るく元気な口調で！";
    QString m_shoutoutPrefix = "【レイド感謝】";
    QString m_lastShoutoutUser;
    QString m_twitchChannel;
    QString m_twitchUsername;
    bool m_isShoutoutRequest = false;

    QList<PendingShoutout> m_shoutoutQueue;
    QTimer *m_shoutoutCooldownTimer = nullptr;
    QTimer *m_shoutoutUiTimer = nullptr;
    qint64 m_shoutoutCooldownStartMs = 0;

    void processNextShoutoutInQueue();
    void updateShoutoutUiStatus();

public:
    explicit AIClientManager(QObject *parent = nullptr);
    ~AIClientManager();
    void loadSettingsFromJsonObject(const QJsonObject &obj);
    void handleRaidShoutout(const QString &username);
    void setAIProvider(const QString &provider, bool forceRefresh = false);

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

    RateLimitTracker& tracker() { return m_tracker; }
    const RateLimitTracker& tracker() const { return m_tracker; }
    bool managerEnabled() const { return m_managerEnabled; }
    QString managerProvider() const { return m_managerProvider; }
    QString managerModel() const { return m_managerModel; }
    QString groqModel() const { return m_groqModel; }

    // 暗号化バックアップの復号・読み出し用I/F
    QList<QPair<QString, QString>> loadObfuscatedBackup(const QString &filePath);

signals:
    void notifyEvent(const AppEvent &event);
    void chatHistoryUpdated(const QList<QPair<QString, QString>> &history); // 履歴更新シグナル
    void userNamesUpdated(const QJsonObject &data);
    void knowledgeMetadataUpdated(const QJsonObject &data);
    void providerStatusResponse(const ProviderStatus &status);

public slots:
    void on_requestAI(const QString &prompt, const QString &user = "");
    void on_twitchRaidReceived(const QString &username);
    void on_shoutoutSuccessReceived(const QString &username);
    void on_clientRequestFinished(const QString &responseText, bool success);
    void resetSession(bool isManual); // セッションリセット機能
    void forceSummarizeHistory(); // 手動強制サマリ化
    QList<ConversationEntry> getConversationEntries() const; // 会話履歴エントリ取得
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
    void on_requestProviderStatus(const QString &providerId);

    QJsonObject userNamesObj() const { return m_userNamesObj; }
};
