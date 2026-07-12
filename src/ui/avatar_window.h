#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QPixmap>
#include <QPoint>
#include <QMap>
#include <QVector>
#include <QTimer>
#include <QQueue>
#include <QJsonObject>
#include "../app_event.h"
#include "../ai/provider_status.h"

struct ImageSetting {
    QString filePath;
    int anchorX = 0;
    int anchorY = 0;
    int transparentX = 0;
    int transparentY = 0;
};

struct FrontVariantEntry {
    QString filePath;  // 画像ファイルパス
    int weight = 1;    // 出現重み（大きいほど出やすい）
};

struct FrontVariantSettings {
    QString label;               // 表示名（メニュー用）
    QVector<FrontVariantEntry> entries;
    int anchorX = 100;
    int anchorY = 100;
    int transparentX = 0;
    int transparentY = 0;
    int intervalMs = 5000;

    bool isEmpty() const { return entries.isEmpty(); }
};

// フレーム順に連続再生するアニメーションシーケンス
struct AnimationSequence {
    QString label;
    QVector<QString> frames;
    int anchorX = 100;
    int anchorY = 100;
    int transparentX = 0;
    int transparentY = 0;
    int frameIntervalMs = 150;
    bool loop = true;
};

// パターンスケジューラーの1エントリ
struct PatternSchedulerEntry {
    QString type;       // "variant_group" または "animation"
    QString name;       // グループ名 / アニメーション名
    int weight = 1;     // 選択重み
    int stayMs = 8000;  // variant_group の場合の滞在時間（ms）
};

class QLineEdit;
class QPushButton;
class QTextBrowser;
class QTableWidget;
class QTabWidget;
class QComboBox;
class QWebSocketServer;
class QWebSocket;
class QCheckBox;
class QNetworkAccessManager;
class QNetworkReply;

class AvatarWindow : public QMainWindow {
    Q_OBJECT
private:
    QLabel *m_avatarLabel;
    QLineEdit *m_inputEdit = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_sttButton = nullptr;
    QPushButton *m_menuButton = nullptr;
    
    // ニックネーム管理用UI
    QTabWidget *m_tabWidget = nullptr;
    QWidget *m_chatTab = nullptr;
    QWidget *m_settingsTab = nullptr;
    QWidget *m_aiSettingsTab = nullptr;
    QWidget *m_nicknameTab = nullptr;
    QTableWidget *m_usersTable = nullptr;
    QTableWidget *m_requestsTable = nullptr;
    QJsonObject m_cachedUserNamesData;

    // ナレッジ管理用UI
    QWidget *m_knowledgeTab = nullptr;
    QTableWidget *m_knowledgeTable = nullptr;
    QPushButton *m_deleteKnowledgeButton = nullptr;
    QJsonObject m_cachedKnowledgeData;
    QLineEdit *m_wsPortEdit = nullptr;
    QLineEdit *m_twitchChannelEdit = nullptr;
    QLineEdit *m_twitchClientIdEdit = nullptr;
    QLineEdit *m_twitchPortEdit = nullptr;
    QLineEdit *m_twitchWakeWordEdit = nullptr;
    QComboBox *m_twitchWakeWordModeCombo = nullptr;
    QCheckBox *m_aiProviderMistralCheckbox = nullptr;
    QCheckBox *m_aiProviderCerebrasCheckbox = nullptr;
    QCheckBox *m_aiProviderGroqCheckbox = nullptr; // Groq
    QLineEdit *m_aiApiKeyEdit = nullptr;
    QLineEdit *m_aiCerebrasApiKeyEdit = nullptr;
    QComboBox *m_aiCerebrasModelCombo = nullptr;
    QLineEdit *m_aiGroqApiKeyEdit = nullptr; // Groq API Key
    QComboBox *m_aiGroqModelCombo = nullptr; // Groq Model
    QLineEdit *m_tavilyApiKeyEdit = nullptr;

    // マネージャAI設定用UI
    QCheckBox *m_managerEnabledCheckbox = nullptr;
    QComboBox *m_managerProviderCombo = nullptr;
    QComboBox *m_managerModelCombo = nullptr;

    // プロバイダ限界設定用UI
    QComboBox *m_limitProviderCombo = nullptr;
    QLineEdit *m_limitRpmEdit = nullptr;
    QLineEdit *m_limitRpdEdit = nullptr;
    QLineEdit *m_limitTpmEdit = nullptr;
    QLineEdit *m_limitTpdEdit = nullptr;
    QLineEdit *m_limitContextEdit = nullptr;
    QCheckBox *m_limitToolCallCheckbox = nullptr;
    QLineEdit *m_limitCostEdit = nullptr;
    QLabel *m_limitRemainingLabel = nullptr;
    QPushButton *m_limitAutoFetchButton = nullptr;

    QNetworkAccessManager *m_modelsNetworkManager = nullptr;
    QLineEdit *m_webhookUrlEdit = nullptr;
    QCheckBox *m_webhookEnabledCheckbox = nullptr;
    QCheckBox *m_discordEnabledCheckbox = nullptr;
    QCheckBox *m_twitchGreetingCheckbox = nullptr;
    QCheckBox *m_discordGreetingCheckbox = nullptr;
    QLineEdit *m_discordBotTokenEdit = nullptr;
    QLineEdit *m_discordChannelIdEdit = nullptr;
    QLineEdit *m_bubbleShortEdit = nullptr;
    QLineEdit *m_bubbleLongEdit = nullptr;
    QLineEdit *m_obsPathEdit = nullptr;
    QLineEdit *m_avatarNameEdit = nullptr;
    QCheckBox *m_nameReactionCheckbox = nullptr;
    QCheckBox *m_obsHttpEnabledCheckbox = nullptr;
    QLineEdit *m_obsHttpPortEdit = nullptr;

    int m_bubbleDisplayShortSec = 5;
    int m_bubbleDisplayLongSec = 10;
    QString m_avatarName = "AIアシスタント";
    bool m_nameReactionEnabled = true;
    QQueue<QPair<QString, QString>> m_aiRequestQueue;
    bool m_isProcessingAI = false;

    void enqueueRequest(const QString &text, const QString &user = "");
    void processNextRequest();

    QString m_twitchOAuthToken;
    QString m_twitchUsername;
    QString m_webhookUrl;
    bool m_webhookEnabled = false;

    // WebHook送信用NetworkManager
    QNetworkAccessManager *m_webhookNetworkManager = nullptr;

    // OBS配信用WebSocketサーバー
    QWebSocketServer *m_wsServer = nullptr;
    QList<QWebSocket *> m_wsClients;
    QString m_lastResponseText;

    QWidget *m_rightPanel = nullptr;
    QTextBrowser *m_responseBrowser = nullptr;

    QMap<QString, ImageSetting> m_imageSettings;      // 状態ごとの設定
    QMap<QString, QPixmap> m_pixmapCache;             // 透過処理済みのキャッシュ
    QString m_currentState;                           // "idle", "thinking" 等
    QPoint m_desktopTargetPos;                        // アバター表示の基準目標座標
    QPoint m_lastWindowPos;                           // ドラッグ後の最後のウィンドウ位置を保存
    bool m_isInitialized = false;                     // 初期配置済みかどうかのフラグ
    

    // バリアントグループ（front_variants / back_variants 等）の汎用管理
    QMap<QString, FrontVariantSettings> m_allVariantGroups;   // 全グループ定義
    QMap<QString, QVector<QPixmap>> m_allVariantCaches;       // 全グループのキャッシュ
    QMap<QString, QVector<int>> m_allVariantWeights;          // 全グループの累積重み
    QString m_activeVariantGroupName;                         // 現在アクティブなグループ名
    int m_currentFrontIndex = 0;         // 現在表示中のインデックス
    bool m_isFrontVariantMode = false;   // バリアントモード中フラグ
    QTimer *m_variantTimer = nullptr;    // 切り替えタイマー

    // シーケンシャルアニメーション用
    QMap<QString, AnimationSequence> m_animations;
    QMap<QString, QVector<QPixmap>> m_animPixmapCache;
    QString m_currentAnimation;
    int m_animFrameIndex = 0;
    QTimer *m_animTimer = nullptr;
    bool m_animAutoPlay = false;  // スケジューラー自動再生中フラグ

    // パターンスケジューラー
    QVector<PatternSchedulerEntry> m_schedulerEntries;
    QVector<int> m_schedulerWeights;
    QString m_lastScheduledName;
    QTimer *m_schedulerTimer = nullptr;
    bool m_schedulerEnabled = false;
    bool m_schedulerPaused = false;   // AI 応答中は一時停止
    QTimer *m_resumeTimer = nullptr;  // Speaking 後に自動再開するタイマー
    void loadSettings();
    void processAndCacheImages();
    QPixmap applyTransparency(const QString &filePath, int tx, int ty);
    void updateAvatarDisplay(const QString &state);
    void updateWindowPosition();
    void switchToNextVariant();
    void switchVariantGroup(const QString &groupName);
    void playAnimation(const QString &name, bool autoPlay = false);
    void stepAnimationFrame();
    void pickNextPattern();
    void pauseScheduler();    // AI 処理中にスケジューラーを停止
    void resumeScheduler();   // AI 処理完了後にスケジューラーを再開
    void showContextMenu(const QPoint &globalPos);

    void initSettingsTab(QWidget *parent);
    void initAiSettingsTab(QWidget *parent);
    void initNicknameTab(QWidget *parent);
    void initKnowledgeTab(QWidget *parent);
    void updateNicknameTables();
    void updateKnowledgeTable();
    void loadSettingsToUI();
    void saveSettingsFromUI();
    void startWebSocketServer();
    void stopWebSocketServer();
    void broadcastToOBS(const QJsonObject &json);
    void notifyAvatarChanged();
    void sendWebHookNotification(const QJsonObject &json);

private slots:
    void onSendClicked();
    void onSttClicked();
    void onMenuClicked();
    void onSaveSettingsClicked();
    void onTwitchReauthClicked();
    void onNewWSConnection();
    void onWSClientDisconnected();
    void onWebHookReplyFinished(QNetworkReply *reply);
    void onCopyObsPathClicked();
    void onApproveRequestClicked();
    void onRejectRequestClicked();
    void onDeleteUserClicked();
    void onAddUserClicked();
    void onUserTableCellChanged(int row, int column);
    void onDeleteKnowledgeClicked();
    void onLimitProviderChanged(int index);
    void onLimitAutoFetchClicked();
    void onModelsReplyFinished(QNetworkReply *reply);

protected:
    void moveEvent(QMoveEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

public:
    explicit AvatarWindow(QWidget *parent = nullptr);
    ~AvatarWindow();

signals:
    // コアスレッドへの要求シグナル
    void startSTTRequested();
    void directInputSubmitted(const QString &text);
    void requestAIExecution(const QString &text, const QString &user);
    void resetSessionRequested(); // 会話履歴リセット要求シグナル
    void importSessionRequested(const QString &filePath);
    void exportSessionRequested(const QString &encPath, const QString &txtPath);
    void settingsUpdated();
    void twitchReauthRequested();
    void deleteKnowledgeRequested(const QString &id);
    void requestKnowledgeMetadataRequested();
    void requestProviderStatus(const QString &providerId);

    // ニックネーム管理用要求シグナル
    void approveNicknameRequested(const QString &requester, const QString &target, const QString &nickname);
    void rejectNicknameRequested(const QString &requester, const QString &target, const QString &nickname);
    void deleteNicknameRequested(const QString &user);
    void updateNicknamePreferredRequested(const QString &user, const QString &preferred);

public slots:
    // コアから通知を受け取るスロット
    void on_notify_events(const AppEvent &event);
    void onNicknameDataUpdated(const QJsonObject &data);
    void onKnowledgeDataUpdated(const QJsonObject &data);
    void onProviderStatusReceived(const ProviderStatus &status);
};
