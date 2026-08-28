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
#include "manager_context_evaluator.h"
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
    // ※ loadCredentials() は public slots へ移動済み（スレッド安全のため invokeMethod 対応）
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

    // --- F-40 複数ユーザー文脈判定・聞き返し状態管理 ---
    QList<ChatMessageEntry> m_chatLogs;
    PendingClarification m_pendingClarification;


    // --- F-15/F-16/F-32/F-39 AIプロバイダ追加 ---
    class MistralAIClient *m_mistralClient = nullptr;
    class GroqAIClient *m_groqClient = nullptr;
    class GeminiAIClient *m_geminiClient = nullptr;
    class HuggingFaceAIClient *m_huggingfaceClient = nullptr;
    class OpenRouterAIClient *m_openrouterClient = nullptr;
    class SakuraAIClient *m_sakuraClient = nullptr;
    class DummyAIClient *m_dummyClient = nullptr;

    QMap<QString, IAIClient*> m_clientMap;
    RateLimitTracker m_tracker;
    AIRouter m_router;
    MarkdownTableEngine m_tableEngine;
    class SearchManager *m_searchManager = nullptr;

    bool m_managerEnabled = false;
    QString m_managerProvider = "groq";
    QString m_managerModel = "llama-3.1-8b-instant";
    QString m_groqModel = "llama-3.3-70b-versatile";
    QString m_geminiModel = "gemini-2.0-flash";
    QString m_mistralModel = "mistral-small-latest";
    QString m_huggingfaceModel = "meta-llama/Llama-3.1-8B-Instruct";
    QString m_openrouterModel = "meta-llama/llama-3.1-8b-instruct:free";
    QString m_sakuraModel = "sakura-llm";
    QString m_tavilyApiKey;
    bool m_taskFlowEnabled = true;
    QString m_taskFlowApiUrl;

    // TaskFlow外部スケジュールAPI連携RAG
    QString getTaskFlowSchedulesContext();
    qint64 m_apiCallStartTimeMs = 0;

    QStringList managerPriorityOrder() const;

    struct PendingRequest {
        QString prompt;
        QString user;
        QString source;
    };
    QList<PendingRequest> m_pendingRequests;
    void processPendingRequests();
    bool selectAndPrepareClient(const QString &prompt = "");

    QStringList m_fallbackProviders;  // F-33: APIキー設定済みプロバイダ（選択中プロバイダ除く）の優先順リスト
    int m_fallbackIndex = 0;          // F-33: 現在のフォールバック試行インデックス

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
    QString m_currentSource = "UI"; // リクエスト入力ソース ("UI", "Twitch", "Discord")
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
    // --- F-34 回答情報量制御（3段階回答モード） ---
    enum class ResponseDetailMode {
        Short,
        Normal,
        Detailed
    };

    static ResponseDetailMode determineResponseDetailMode(const QString &prompt, bool *isGranularityReduction = nullptr);
    static QString formatResponseDetailInstruction(ResponseDetailMode mode, bool isGranularityReduction = false);

    // --- 段階的タスク実行パイプライン ---
    enum class TaskType {
        KnowledgeSearch,
        WebSearchRAG
    };

    struct ExecutionTask {
        TaskType type;
        QString queryKeyword;
        QString extractedData;
        bool isCompleted = false;
    };

    QList<ExecutionTask> analyzeAndDecomposeTasks(const QString &prompt);
    QString generateRefinedQuery(const QString &rawQuerySentence, const QString &targetType = "weather");
    void executeTaskPipeline(QList<ExecutionTask> &tasks);
    void validateAndInjectGuards(const QList<ExecutionTask> &tasks, const QString &originalPrompt, QString &additionalSystemPrompt);
    QString formatCombinedPrompt(const QList<ExecutionTask> &tasks, const QString &originalPrompt);
    QString fetchSchedules(const QString &category, const class QDate &startDate, int days);

    // --- F-35 CommunityObserver 連携 ---
    QString evaluateWithObserver(const QString &platform, const QString &user, const QString &text);

private:



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

    bool m_useProcessForIntro = true;
    bool m_isMockHelix = false;

    void clearRequestState();
    void triggerShoutout(const QString &username);
    void processNextShoutoutInQueue();
    void updateShoutoutUiStatus();

public:
    explicit AIClientManager(QObject *parent = nullptr);
    ~AIClientManager();
    void loadSettingsFromJsonObject(const QJsonObject &obj);
    void handleRaidShoutout(const QString &username, const QVariantMap &meta = {});
    // 会話トリガーからのクリエイター紹介（呼び出し元ソースを引き継ぎ、レイド文脈と分離）
    void handleConversationShoutout(const QString &username, const QString &source, const QString &twitchChannel = {});
    void setAIProvider(const QString &provider, bool forceRefresh = false);
    void setHelixClient(TwitchHelixClient *client) {
        m_helixClient = client;
        m_isMockHelix = (client != nullptr);
    }
    TwitchHelixClient* helixClient() const { return m_helixClient; }

    // 履歴データ取得/設定用のI/F
    QList<QPair<QString, QString>> chatHistory() const { return m_chatHistory; }
    void setChatHistory(const QList<QPair<QString, QString>> &history) {
        m_chatHistory = history;
        emit chatHistoryUpdated(m_chatHistory);
    }

    KnowledgeImportState importState() const { return m_importState; }
    QJsonObject knowledgeMetadata() const { return m_knowledgeMetadata; }
    QString lastFinalPrompt() const { return m_lastFinalPrompt; }
    QString lastAdditionalSystemPrompt() const { return m_lastAdditionalSystemPrompt; }
    Q_INVOKABLE QString avatarName() const { return m_avatarName; }

    RateLimitTracker& tracker() { return m_tracker; }
    const RateLimitTracker& tracker() const { return m_tracker; }
    bool managerEnabled() const { return m_managerEnabled; }
    QString managerProvider() const { return m_managerProvider; }
    QString managerModel() const { return m_managerModel; }
    QString groqModel() const { return m_groqModel; }

    const PendingClarification& pendingClarification() const { return m_pendingClarification; }
    void setPendingClarification(const PendingClarification &p) { m_pendingClarification = p; }
    const QList<ChatMessageEntry>& chatLogs() const { return m_chatLogs; }
    void addChatLogEntry(const QString &sender, bool isAssistant, const QString &text);


    void setUseProcessForIntro(bool enabled) { m_useProcessForIntro = enabled; }
    bool useProcessForIntro() const { return m_useProcessForIntro; }
    bool isMockHelix() const { return m_isMockHelix; }

    // 暗号化バックアップの復号・読み出し用I/F
    QList<QPair<QString, QString>> loadObfuscatedBackup(const QString &filePath);

    // --- F-33: テストから呼び出し可能なヘルパー ---
    QStringList workerPriorityOrder() const;
    void buildFallbackProviderList(); // loadCredentials() 後に呼んでリストを構築する
    QString buildHumanReadableError(int httpCode, const QString &providerId,
                                    const QJsonObject &errorJson) const;

    // レイド歓迎プロンプトの構築（helixClient不要・テスト可能な純粋関数）
    static QString buildRaidShoutoutPrompt(
        const QString &login, const QString &displayName,
        const QString &bio, const QString &game,
        const QStringList &recentGames, const QString &title,
        const QString &sns,
        const QString &lengthHint = "2〜3文", const QString &tone = "明るく親しみやすい");

    // 会話トリガー向け中立クリエイター紹介プロンプトの構築（テスト可能な純粋関数）
    static QString buildConversationShoutoutPrompt(
        const QString &login, const QString &displayName,
        const QString &bio, const QString &game,
        const QStringList &recentGames, const QString &title,
        const QString &sns,
        const QString &lengthHint = "2〜3文", const QString &tone = "明るく親しみやすい");

signals:
    void notifyEvent(const AppEvent &event);
    void chatHistoryUpdated(const QList<QPair<QString, QString>> &history); // 履歴更新シグナル
    void userNamesUpdated(const QJsonObject &data);
    void knowledgeMetadataUpdated(const QJsonObject &data);
    void providerStatusResponse(const ProviderStatus &status);
    /// RateLimitTabWidget 向け: aiThread 上でトラッカーが更新された際に emit
    void rateLimitStatusUpdated(const QList<ProviderStatus> &statuses);

public slots:
    void loadCredentials();         // スレッド安全な呼び出しのため public slot 化
    void emitCurrentStatus();       // RateLimitTabWidget 初期化時に現在状態を通知する
    void on_requestAI(const QString &prompt, const QString &user = "", const QString &source = "UI");
    static QString formatSpeakerTaggedPrompt(const QString &prompt, const QString &speaker, const QString &target = "", const QString &categoryStr = "");

    void on_twitchRaidReceived(const QString &username, const QVariantMap &meta = {});
    void on_shoutoutSuccessReceived(const QString &username);
    void on_clientRequestFinished(const QString &responseText, bool success, int httpCode);
    void resetSession(bool isManual); // セッションリセット機能
    void forceSummarizeHistory(); // 手動強制サマリ化
    QList<ConversationEntry> getConversationEntries() const; // 会話履歴エントリ取得
    bool importSessionBackup(const QString &filePath);
    void exportSessionBackup(const QString &encPath, const QString &txtPath);
    void on_settingsUpdated();

    // ニックネーム管理用
    Q_INVOKABLE QString handleNicknameUpdateRequest(const QString &target, const QString &nickname);
    void saveUserNames();
    void approveNicknameRequest(const QString &requester, const QString &target, const QString &nickname);
    void rejectNicknameRequest(const QString &requester, const QString &target, const QString &nickname);
    void deleteNickname(const QString &user);
    void updateNicknamePreferred(const QString &user, const QString &preferred);
    void updateUserMapping(const QString &profileId, const QString &preferred, const QString &twitchId, const QString &discordId, const QStringList &nicknames = {});
    void mergeUserProfiles(const QString &targetProfileId, const QString &sourceProfileId);
    QJsonObject findUserProfile(const QString &userLower, QString *outProfileKey = nullptr) const;

    // ナレッジ管理スロット
    void deleteKnowledge(const QString &id);
    void on_requestKnowledgeMetadata();
    void onImportTimeout();
    Q_INVOKABLE QString finalizeKnowledgeImport(const QString &title, const QString &description, const QStringList &keywords);
    Q_INVOKABLE bool isKnowledgeImportQandAMode() const { return m_importState == KnowledgeImportState::QandAMode; }
    void openKnowledgeInputFolder();
    void on_requestProviderStatus(const QString &providerId);
    Q_INVOKABLE void updateRateLimitFromReply(const QString &providerId, QNetworkReply *reply);

    QJsonObject userNamesObj() const { return m_userNamesObj; }
};

