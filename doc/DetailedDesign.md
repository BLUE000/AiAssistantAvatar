# 詳細設計書 - AI Assistant Avatar

## 1. 開発・ビルド環境
本アプリケーションは、Windows環境において以下の構成でビルドおよび実行される。

* **開発言語:** C++20 (`MSVC` または `MinGW`)
* **GUIフレームワーク:** Qt 6 (QtCore, QtGui, QtWidgets, QtNetwork)
* **ビルドシステム:** CMake 3.20以上
* **依存外部ライブラリ:**
  - **whisper.cpp:** ローカル音声認識用（プロジェクト内にソースコードを含める、もしくは事前ビルドした `.lib`/`.dll` をリンク）。
  - **Windows SDK (SAPI):** Windows標準音声認識用（COM APIを使用するため、Windows標準ライブラリ `sapi.h` および `ole32.lib` 等をリンク）。
* **設定ファイル解析:** Qt標準的 `QJsonDocument`, `QJsonObject` を使用し、外部依存を最小化する。

---

## 2. ディレクトリ構成
ソースコードおよびリソースは以下のフォルダ構成で配置する。

```text
AiAssistantAvatar/
├── CMakeLists.txt
├── doc/                         # 各種設計ドキュメント
│   ├── Requirements.md
│   ├── BasicDesign.md
│   └── DetailedDesign.md
├── Lib/                         # サブプロジェクト (Git Submodule)
│   ├── CMakeLists.txt
│   ├── TransCipher/             # 暗号化/難読化ライブラリ
│   └── TrustChain/              # 出自証明/改ざん検知ライブラリ
├── pic/                         # 画像およびアバター設定JSON
│   ├── avatar_settings.json
│   ├── idle.png
│   ├── listening.png
│   ├── thinking.png
│   └── speaking.png
└── src/                         # ソースコード
    ├── main.cpp
    ├── app_event.h
    ├── core_module.h
    ├── core_module.cpp
    ├── ui/                      # UIモジュール
    │   ├── avatar_window.h
    │   ├── avatar_window.cpp
    │   ├── balloon_widget.h
    │   └── balloon_widget.cpp
    ├── twitch/                  # Twitchモジュール
    │   ├── twitch_reader.h
    │   └── twitch_reader.cpp
    ├── stt/                     # 音声認識モジュール
    │   ├── stt_manager.h
    │   ├── stt_manager.cpp
    │   ├── istt_engine.h
    │   ├── whisper_engine.h
    │   ├── whisper_engine.cpp
    │   ├── sapi_engine.h
    │   └── sapi_engine.cpp
    └── ai/                      # AIモジュール
        ├── ai_client_manager.h
        ├── ai_client_manager.cpp
        ├── iai_client.h
        ├── mistral_ai_client.h
        ├── mistral_ai_client.cpp
        ├── cerebras_ai_client.h
        ├── cerebras_ai_client.cpp
        ├── dummy_ai_client.h
        ├── dummy_ai_client.cpp
        ├── search_manager.h
        ├── search_manager.cpp
        ├── isearch_provider.h
        ├── tavily_search_provider.h
        ├── tavily_search_provider.cpp
        ├── duckduckgo_search_provider.h
        └── duckduckgo_search_provider.cpp
```

---

## 3. 各モジュールの詳細クラス設計

### 3.1 共通・コアモジュール

#### A. `app_event.h` (共通イベント構造体)
スレッド間でメッセージをやり取りするためのデータ定義。
```cpp
#pragma once
#include <QString>
#include <QVariantMap>

enum class EventType {
    TwitchCommentReceived,  // 対象のコメント受信
    DiscordMessageReceived, // Discordメッセージ受信
    VoiceInputStarted,       // 音声認識開始
    VoiceInputCompleted,     // 音声認識完了 (テキスト有り)
    DirectInputSubmitted,    // キーボード直接入力
    AIRequestSent,          // AIへ送信開始
    AIResponseReceived,     // AIからの回答受信
    ErrorOccurred          // エラー発生
};

struct AppEvent {
    EventType type;
    QString text;
    QString source;
    QVariantMap extraData;
};
```

#### B. `CoreModule` クラス
全体のコントロールを司る常駐オブジェクト。
```cpp
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
    void requestAI(const QString &prompt, const QString &user = "");
    void requestSessionReset(bool isManual);
    void requestSessionImport(const QString &filePath);
    void requestSessionExport(const QString &encPath, const QString &txtPath);
    void settingsUpdated();
    void requestTwitchReauth();
    void requestDiscordSend(const QString &channelId, const QString &text);
    void requestDeleteKnowledge(const QString &id);
    void requestKnowledgeMetadata();

public slots:
    // 他モジュール（Twitch, STT, AI）からのイベントを受け取るスロット
    void on_notify_events(const AppEvent &event);

    // UIからの直接命令を受け取るスロット (別スレッドからQueuedで呼ばれる)
    void on_startSTTRequested();
    void on_directInputSubmitted(const QString &text);
    void on_resetSessionRequested();
    void on_importSessionRequested(const QString &filePath);
    void on_exportSessionRequested(const QString &encPath, const QString &txtPath);
    void on_settingsUpdated();
    void on_twitchReauthRequested();
    void on_deleteKnowledgeRequested(const QString &id);
    void on_requestKnowledgeMetadata();
};
```

---

### 3.2 UIモジュール

#### A. `AvatarWindow` クラス (通常メインウィンドウ)
デスクトップ上にアバターを表示し、設定に基づきアンカー移動を行う。
```cpp
#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QPixmap>
#include <QPoint>
#include <QMap>
#include "app_event.h"

struct ImageSetting {
    QString filePath;
    int anchorX = 0;
    int anchorY = 0;
    int transparentX = 0;
    int transparentY = 0;
};

class QLineEdit;
class QPushButton;
class QTextBrowser;
class QTabWidget;
class QComboBox;
class QWebSocketServer;
class QWebSocket;

class AvatarWindow : public QMainWindow {
    Q_OBJECT
private:
    QLabel *m_avatarLabel;
    
    // UIコントロール
    QWidget *m_leftPanel = nullptr;
    QTabWidget *m_tabWidget = nullptr;
    QLineEdit *m_inputEdit = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_sttButton = nullptr;
    QPushButton *m_menuButton = nullptr;
    
    // 設定タブ・AI設定タブ用UI
    QWidget *m_aiSettingsTab = nullptr;
    QLineEdit *m_wsPortEdit = nullptr;
    QLineEdit *m_twitchChannelEdit = nullptr;
    QLineEdit *m_twitchClientIdEdit = nullptr;
    QLineEdit *m_twitchPortEdit = nullptr;
    QLineEdit *m_twitchWakeWordEdit = nullptr;
    QComboBox *m_twitchWakeWordModeCombo = nullptr;
    QCheckBox *m_aiProviderMistralCheckbox = nullptr;
    QCheckBox *m_aiProviderCerebrasCheckbox = nullptr;
    QLineEdit *m_aiApiKeyEdit = nullptr;
    QLineEdit *m_aiCerebrasApiKeyEdit = nullptr;
    QComboBox *m_aiCerebrasModelCombo = nullptr;
    QLineEdit *m_tavilyApiKeyEdit = nullptr;
    QCheckBox *m_webhookEnabledCheckbox = nullptr;
    QCheckBox *m_twitchGreetingCheckbox = nullptr;
    QCheckBox *m_discordGreetingCheckbox = nullptr;
    
    // OBS配信用WebSocketサーバー
    QWebSocketServer *m_wsServer = nullptr;
    QList<QWebSocket *> m_wsClients;

    // WebHook送信用NetworkManager
    QNetworkAccessManager *m_webhookNetworkManager = nullptr;
    QString m_webhookUrl;
    bool m_webhookEnabled = false;

    QWidget *m_rightPanel = nullptr;
    QTextBrowser *m_responseBrowser = nullptr;

    QMap<QString, ImageSetting> m_imageSettings; // 状態ごとの設定
    QMap<QString, QPixmap> m_pixmapCache;        // 透過処理済みのキャッシュ
    QString m_currentState;                      // "idle", "thinking" 等
    QPoint m_desktopTargetPos;                   // アバター表示の基準目標座標
    QPoint m_dragPosition;                       // ドラッグ用一時座標
    QPoint m_lastWindowPos;                      // ドラッグ後の最後のウィンドウ位置を保存
    bool m_userDraggedWindow = false;            // ユーザーがドラッグで移動したかどうかのフラグ
    
    // ニックネーム管理用UI
    QTableWidget *m_nicknameTable = nullptr;
    QTableWidget *m_pendingTable = nullptr;
    QPushButton *m_approveButton = nullptr;
    QPushButton *m_rejectButton = nullptr;
    QPushButton *m_deleteUserButton = nullptr;
    QPushButton *m_addUserButton = nullptr;
    QJsonObject m_cachedUserNamesData;

    // ナレッジ管理用UI
    QTableWidget *m_knowledgeTable = nullptr;
    QPushButton *m_deleteKnowledgeButton = nullptr;
    QJsonObject m_cachedKnowledgeData;
    
    void loadSettings();
    void processAndCacheImages();
    QPixmap applyTransparency(const QString &filePath, int tx, int ty);
    void updateAvatarDisplay(const QString &state);
    void updateWindowPosition();
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
    void onApproveRequestClicked();
    void onRejectRequestClicked();
    void onDeleteUserClicked();
    void onAddUserClicked();
    void onNicknameTableDoubleClicked(int row, int column);
    void onDeleteKnowledgeClicked();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

public:
    explicit AvatarWindow(QWidget *parent = nullptr);
    ~AvatarWindow();

signals:
    // コアスレッドへの要求シグナル
    void startSTTRequested();
    void directInputSubmitted(const QString &text);
    void resetSessionRequested();
    void importSessionRequested(const QString &filePath);
    void exportSessionRequested(const QString &encPath, const QString &txtPath);
    void settingsUpdated();
    void twitchReauthRequested();
    void deleteKnowledgeRequested(const QString &id);
    void requestKnowledgeMetadataRequested();
    
    // ニックネーム操作要求
    void approveNicknameRequested(const QString &requester, const QString &target, const QString &nickname);
    void rejectNicknameRequested(const QString &requester, const QString &target, const QString &nickname);
    void deleteNicknameRequested(const QString &user);
    void updateNicknamePreferredRequested(const QString &user, const QString &preferred);

public slots:
    // コアから通知を受け取るスロット
    void on_notify_events(const AppEvent &event);
    void onNicknameDataUpdated(const QJsonObject &data);
    void onKnowledgeDataUpdated(const QJsonObject &data);
};
```

---

#### B. 右側ペイン（QTextBrowser）の吹き出し風装飾仕様
右側ペイン (`m_rightPanel`) はアバターの横に配置され、吹き出しのような外観にスタイルシートで装飾する。最新のAIの回答のみを表示する。

**特性:**
- **背景色**: 半透過白（rgba(245, 245, 245, 240)）
- **枠線**: 薄いグレー（rgba(180, 180, 180, 200)）、幅 1.5px
- **角丸**: `10px`
- **テキスト**: `m_responseBrowser` (QTextBrowser) により、最新のAI応答を枠なし・背景透明でマークダウン描画。必要に応じてスクロール可能。

---


### 3.3 Twitchモジュール

#### A. `TwitchReader` クラス
OAuth認可コードフローおよびリフレッシュトークンによる自動更新に対応したチャット監視モジュール。
```cpp
#pragma once
#include <QObject>
#include <QWebSocket>
#include <QTcpServer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include "../app_event.h"

class TwitchReader : public QObject {
    Q_OBJECT
private:
    bool m_isRunning = false;
    QString m_channel;
    QString m_oauthToken;       // アクセストークン
    QString m_clientId;
    QString m_wakeWord;
    QString m_wakeWordMode;     // "contains" または "prefix" / "command"
    int m_authPort = 48080;

    QWebSocket *m_webSocket = nullptr;
    QTcpServer *m_authServer = nullptr;
    QString m_configPath;
    bool m_greetingEnabled = false;

    void loadSettings();
    void saveTokenToSettings(const QString &accessToken);
    void saveOAuthDataToSettings(const QString &accessToken, const QString &channel);
    void fetchChannelName(const QString &token);
    void startOAuthServer();
    void connectToTwitch();

public:
    explicit TwitchReader(QObject *parent = nullptr);
    ~TwitchReader();

    void setSettings(const QString &channel, const QString &token, const QString &clientId, const QString &wakeWord);
    void setWakeWordMode(const QString &mode) { m_wakeWordMode = mode.trimmed().toLower(); }

signals:
    void notifyEvent(const AppEvent &event);

public slots:
    void on_startReading();
    void on_stopReading();
    void on_settingsUpdated();
    void on_twitchReauthRequested();
    void injectTestComment(const QString &user, const QString &message);

private slots:
    void handleNewConnection();
    void onWebSocketConnected();
    void onWebSocketDisconnected();
    void onTextMessageReceived(const QString &message);
};
```

#### B. `DiscordReader` クラス
指定されたDiscordの特定チャンネルを常駐監視し、受信したメッセージをコアへ通知するとともに、AIからの応答を非同期でDiscordのチャンネルへ送信する。
```cpp
#pragma once
#include <QObject>
#include <QWebSocket>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include "../app_event.h"

class DiscordReader : public QObject {
    Q_OBJECT
private:
    bool m_isRunning = false;
    bool m_enabled = false;
    QString m_botToken;
    QString m_channelId;
    QWebSocket *m_webSocket = nullptr;
    QNetworkAccessManager *m_networkManager = nullptr;
    QTimer *m_heartbeatTimer = nullptr;
    bool m_hasAck = true;
    bool m_greetingEnabled = false;

    void loadSettings();
    void connectToDiscord();
    void sendHeartbeat();
    void identify();

public:
    explicit DiscordReader(QObject *parent = nullptr);
    ~DiscordReader();

signals:
    void notifyEvent(const AppEvent &event);

public slots:
    void on_startReading();
    void on_stopReading();
    void on_settingsUpdated();
    void on_requestDiscordSend(const QString &channelId, const QString &text);

private slots:
    void onWebSocketConnected();
    void onWebSocketDisconnected();
    void onTextMessageReceived(const QString &message);
    void onReplyFinished(QNetworkReply *reply);
};
```

---

### 3.4 音声認識 (STT) モジュール

#### A. `ISTTEngine` インターフェース
```cpp
#pragma once
#include <QObject>

class ISTTEngine : public QObject {
    Q_OBJECT
public:
    virtual ~ISTTEngine() = default;
    virtual bool initialize() = 0;
    virtual void startListening() = 0;
    virtual void stopListening() = 0;
    
signals:
    // 文字起こし完了時の内部通知
    void transcriptionFinished(const QString &text, bool success);
};
```

#### B. `STTManager` クラス
マイクキャプチャを管理し、エンジン切り替えを吸収するラッパー。
```cpp
#pragma once
#include <QObject>
#include "istt_engine.h"
#include "app_event.h"

class STTManager : public QObject {
    Q_OBJECT
private:
    ISTTEngine *m_currentEngine = nullptr;
    QString m_engineType; // "whisper" or "sapi"

public:
    explicit STTManager(QObject *parent = nullptr);
    ~STTManager();
    void setEngine(const QString &type);

signals:
    void notifyEvent(const AppEvent &event);

public slots:
    void on_startListening();
    void on_stopListening();
    void on_transcriptionFinished(const QString &text, bool success);
};
```

---

### 3.5 AIモジュール (2段構成)

#### A. `IAIClient` インターフェース (2段目)
```cpp
#pragma once
#include <QObject>

class IAIClient : public QObject {
    Q_OBJECT
public:
    virtual ~IAIClient() = default;
    virtual void sendRequest(const QString &prompt, const QList<QPair<QString, QString>> &history, const QString &sessionContext = QString(), const QString &systemInstruction = QString()) = 0;
    virtual void setApiKey(const QString &apiKey) = 0;

signals:
    void requestFinished(const QString &responseText, bool success);
};
```

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

    QString m_transCipherKey;
    QList<QPair<QString, QString>> m_chatHistory; // ユーザー、AIの対話ペア
    int m_maxHistoryCount = 10; // 自動リセット契機（10件＝5往復）
    QString m_sessionContext; // マークダウンのコンテキスト情報
    bool m_isResetting = false; // 要約要求中かどうかのフラグ
    bool m_isManualReset = false; // 手動リセット中かどうかのフラグ
    QString m_lastPrompt; // 前回のプロンプト
    bool m_blacklistEnabled = true;
    QStringList m_blacklist;
    QStringList m_whitelist;
    bool m_isTranslationRequest = false;

    // ニックネーム管理
    QJsonObject m_userNamesObj;
    QString m_streamerName;
    QString m_currentRequester;

    // 長期記憶想起用
    QString m_recalledContext;
    void scanMemorySummaries(const QString &prompt);
    void loadMemoryDetail(const QString &sessionId);

    // ナレッジ管理・対話登録
    KnowledgeImportState m_importState = KnowledgeImportState::Idle;
    QTimer *m_importTimeoutTimer = nullptr;
    QString m_importingFileName;
    QString m_importingFileContent;
    QJsonObject m_knowledgeMetadata;

    void loadCredentials();
    void loadSessionContext();
    void saveSessionContext(const QString &context);
    void saveObfuscatedLog(const QString &logText);
    QList<QPair<QString, QString>> loadObfuscatedBackup(const QString &filePath);
    void loadBlacklist();
    void loadWhitelist();
    void loadUserNames();
    void saveUserNames();
    QString applyMask(const QString &text) const;
    bool isLanguageIndicator(const QString &lang) const;
    QString mapLanguage(const QString &lang) const;

    // ナレッジ管理ヘルパー
    void loadKnowledgeMetadata();
    void saveKnowledgeMetadata();
    void scanStaticKnowledge(const QString &prompt, QString &recalledPrompt);

public:
    explicit AIClientManager(QObject *parent = nullptr);
    ~AIClientManager();
    void setAIProvider(const QString &provider, bool forceRefresh = false); // "mistral", "cerebras", or "dummy"
    QList<QPair<QString, QString>> getChatHistory() const;
    QJsonObject userNamesObj() const { return m_userNamesObj; }
    KnowledgeImportState importState() const { return m_importState; }

signals:
    void notifyEvent(const AppEvent &event);
    void chatHistoryUpdated(const QList<QPair<QString, QString>> &history);
    void userNamesUpdated(const QJsonObject &data);
    void knowledgeMetadataUpdated(const QJsonObject &data);

public slots:
    void on_requestAI(const QString &prompt, const QString &user = "");
    void on_clientRequestFinished(const QString &responseText, bool success);
    void resetSession(bool isManual);
    bool importSessionBackup(const QString &filePath);
    void exportSessionBackup(const QString &encPath, const QString &txtPath);
    void on_requestChatHistory();

    // ニックネーム管理スロット
    QString handleNicknameUpdateRequest(const QString &target, const QString &nickname);
    void approveNicknameRequest(const QString &requester, const QString &target, const QString &nickname);
    void rejectNicknameRequest(const QString &requester, const QString &target, const QString &nickname);
    void deleteNickname(const QString &user);
    void updateNicknamePreferred(const QString &user, const QString &preferred);

    // ナレッジ管理・タイムアウトスロット
    void deleteKnowledge(const QString &id);
    void on_requestKnowledgeMetadata();
    void onImportTimeout();
    QString finalizeKnowledgeImport(const QString &title, const QString &description, const QStringList &keywords);
    void openKnowledgeInputFolder();
};
```

#### C. `MistralAIClient` クラス
```cpp
#pragma once
#include "iai_client.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonArray>

class SearchManager;

class MistralAIClient : public IAIClient {
    Q_OBJECT
private:
    QNetworkAccessManager *m_networkManager;
    QString m_apiKey;
    SearchManager *m_searchManager;

    // Function Calling 状態管理用
    QString m_pendingPrompt;
    QJsonArray m_pendingMessages;
    QString m_activeToolCallId;
    bool m_isToolCalling;

public:
    explicit MistralAIClient(QObject *parent = nullptr);
    ~MistralAIClient() override;
    void sendRequest(const QString &prompt, const QList<QPair<QString, QString>> &history = {}, const QString &sessionContext = QString(), const QString &systemInstruction = QString()) override;
    void setApiKey(const QString &apiKey) override;
    void setTavilyApiKey(const QString &tavilyKey);

private slots:
    void on_networkReplyFinished(QNetworkReply *reply);
    void on_searchFinished(const QString &resultText, bool success);
};
```

#### D. `CerebrasAIClient` クラス
```cpp
#pragma once
#include "iai_client.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonArray>

class SearchManager;

class CerebrasAIClient : public IAIClient {
    Q_OBJECT
private:
    QNetworkAccessManager *m_networkManager;
    QString m_apiKey;
    QString m_model;
    SearchManager *m_searchManager;

    // Function Calling 状態管理用
    QString m_pendingPrompt;
    QJsonArray m_pendingMessages;
    QString m_activeToolCallId;
    bool m_isToolCalling;

public:
    explicit CerebrasAIClient(QObject *parent = nullptr);
    ~CerebrasAIClient() override;
    void sendRequest(const QString &prompt, const QList<QPair<QString, QString>> &history = {}, const QString &sessionContext = QString(), const QString &systemInstruction = QString()) override;
    void setApiKey(const QString &apiKey) override;
    void setModel(const QString &model);
    void setTavilyApiKey(const QString &tavilyKey);

private slots:
    void on_networkReplyFinished(QNetworkReply *reply);
    void on_searchFinished(const QString &resultText, bool success);
};
```
```

#### D. `SearchManager` クラスおよびプロバイダ群
```cpp
#pragma once
#include <QObject>
#include <QString>

class ISearchProvider;

class SearchManager : public QObject {
    Q_OBJECT
private:
    ISearchProvider *m_currentProvider = nullptr;
    QString m_tavilyApiKey;
    QString m_query;
    bool m_useTavily = false;

    void startNextProvider();

public:
    explicit SearchManager(QObject *parent = nullptr);
    ~SearchManager();
    void setTavilyApiKey(const QString &apiKey);
    void executeSearch(const QString &query);

signals:
    void searchFinished(const QString &resultText, bool success);

private slots:
    void on_providerFinished(const QString &resultText, bool success);
};
```

```cpp
#pragma once
#include <QObject>
#include <QString>

class ISearchProvider : public QObject {
    Q_OBJECT
public:
    virtual ~ISearchProvider() = default;
    virtual void search(const QString &query) = 0;

signals:
    void searchFinished(const QString &resultText, bool success);
};
```

```cpp
#pragma once
#include "isearch_provider.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>

class TavilySearchProvider : public ISearchProvider {
    Q_OBJECT
private:
    QNetworkAccessManager *m_networkManager;
    QString m_apiKey;

public:
    explicit TavilySearchProvider(const QString &apiKey, QObject *parent = nullptr);
    void search(const QString &query) override;

private slots:
    void on_replyFinished(QNetworkReply *reply);
};
```

```cpp
#pragma once
#include "isearch_provider.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>

class DuckDuckGoSearchProvider : public ISearchProvider {
    Q_OBJECT
private:
    QNetworkAccessManager *m_networkManager;

public:
    explicit DuckDuckGoSearchProvider(QObject *parent = nullptr);
    void search(const QString &query) override;

private slots:
    void on_replyFinished(QNetworkReply *reply);
};
```

---

## 4. 重要アルゴリズム・処理仕様

### 4.1 透過処理アルゴリズム (Flood Fill 透過処理)
`AvatarWindow::applyTransparency` にて実行される画像ロード時の透過処理ロジック。

```cpp
QPixmap AvatarWindow::applyTransparency(const QString &filePath, int tx, int ty) {
    QImage image(filePath);
    if (image.isNull()) return QPixmap();

    // アルファチャンネル付きフォーマットに変換
    image = image.convertToFormat(QImage::Format_ARGB32);

    // 指定座標 (tx, ty) の背景色を取得
    QRgb targetColor = image.pixel(tx, ty);
    QColor transColor(0, 0, 0, 0); // 透明色

    // 探索用のキュー
    QList<QPoint> queue;
    queue.append(QPoint(tx, ty));

    // 探索済みマップ
    int width = image.width();
    int height = image.height();
    QVector<QVector<bool>> visited(width, QVector<bool>(height, false));
    visited[tx][ty] = true;

    // 隣接4方向
    const int dx[] = {0, 0, 1, -1};
    const int dy[] = {1, -1, 0, 0};

    // BFS (幅優先探索) による Flood Fill
    while (!queue.isEmpty()) {
        QPoint p = queue.takeFirst();
        
        // 色が一致している場合は透明にする
        if (image.pixel(p) == targetColor) {
            image.setPixelColor(p, transColor);
            
            // 4方向へ探索を進める
            for (int i = 0; i < 4; ++i) {
                int nx = p.x() + dx[i];
                int ny = p.y() + dy[i];
                
                if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                    if (!visited[nx][ny]) {
                        visited[nx][ny] = true;
                        queue.append(QPoint(nx, ny));
                    }
                }
            }
        }
    }
    return QPixmap::fromImage(image);
}
```

### 4.2 位置ズレ補正（アンカー移動）ロジック
画像サイズが切り替わった際に、アバターの表示目標位置 `(targetX, targetY)` にアンカーがぴったりと固定されるようにする計算式。

```cpp
void AvatarWindow::updateWindowPosition() {
    ImageSetting setting = m_imageSettings[m_currentState];
    
    // ウィンドウ自体をアンカー基準で移動
    // targetX, targetY はデスクトップ上のマスコットの足元/基準位置
    int newX = m_desktopTargetPos.x() - setting.anchorX;
    int newY = m_desktopTargetPos.y() - setting.anchorY;
    
    // ウィンドウサイズを現在ピクセルのサイズに適合させる
    QPixmap currentPixmap = m_pixmapCache[m_currentState];
    this->resize(currentPixmap.size());
    m_avatarLabel->resize(currentPixmap.size());
    m_avatarLabel->setPixmap(currentPixmap);
    
    this->move(newX, newY);
}
```

### 4.3 出自検証およびコピーライト動的スキャン仕様 (TrustChain & BinMarkManager)

#### A. `TrustChain::Core` クラス (検証ロジック)
オンラインサーバーへのトークン検証や、セキュリティ判定を処理する。
```cpp
namespace TrustChain {
enum class AuthStatus {
    Normal,         // 検証成功
    Watermarked,    // 改ざん検知・オフライン・通信エラー
    Terminated      // トークン無効化（ブラックリスト）
};

class Core {
public:
    Core();
    AuthStatus verifyToken();
    bool validateTokenSecurity(const QString &token) const;
    static void terminateApplication(const QString &errorMessage = QString());
};
}
```

#### B. `TrustChain::QtHelper` クラス (UI連携 & スキャン)
検証結果に基づいて、実行バイナリ末尾から `BinMarkManager` 署名をパースし、UIを更新する。
```cpp
namespace TrustChain {
class QtHelper {
public:
    // UIへのウォーターマーク適用
    static void applyWatermark(QMainWindow* window, AuthStatus status);
    
    // 指定ファイルの末尾10KBから BinMarkManager 形式の平文コピーライトを抽出
    static QString extractCopyrightFromFile(const QString& filePath);
};
}
```

##### 抽出処理のアルゴリズム (extractCopyrightFromFile)
1. `QFile` をバイナリ読み込みモードで開き、サイズを確認する。
2. 末尾から `10240` バイト（10KB）分シーク（`seek`）して `QByteArray` に読み出す。
3. `[BM_END]` のインデックスと、その手前の `[BM_START]` のインデックスを検索。
4. 二つのタグの間のデータから `"Plain="` を検索し、その開始位置から改行文字（`\n`）までの範囲を切り出し。
5. 前後の空白文字や `\r` を除去して `QString` として返却する。


### 4.4 DuckDuckGo HTMLパースおよびHTMLエンティティデコードロジック
DuckDuckGo HTML版の検索結果スニペット（要約）およびURL情報を正規表現で抽出し、HTMLタグやHTMLエンティティを除去・デコードする。

```cpp
// 検索結果パース用正規表現例
QRegularExpression snippetRegex("<a class=\"result__snippet\"[^>]*>([\\s\\S]*?)</a>");
QRegularExpression linkRegex("<a class=\"result__url\" href=\"([^\"]*)\">");

// HTMLタグの除去
QString cleanText = rawText;
cleanText.remove(QRegularExpression("<[^>]*>"));

// 主要なHTMLエンティティのデコード
cleanText.replace("&amp;", "&");
cleanText.replace("&quot;", "\"");
cleanText.replace("&#x27;", "'");
cleanText.replace("&lt;", "<");
cleanText.replace("&gt;", ">");
cleanText.replace("&#x2F;", "/");
```

### 4.5 TavilyからDuckDuckGoへの自動フォールバック制御
`SearchManager` が各検索プロバイダを順次起動し、Tavily API 接続時に何らかの通信エラーや HTTP エラー（キー無効、無料枠終了）が発生した際に、自動的かつサイレントに DuckDuckGo プロバイダへフォールバックして処理を継続する。

```cpp
void SearchManager::executeSearch(const QString &query) {
    m_query = query;
    m_useTavily = !m_tavilyApiKey.isEmpty();
    startNextProvider();
}

void SearchManager::startNextProvider() {
    if (m_currentProvider) {
        m_currentProvider->deleteLater();
        m_currentProvider = nullptr;
    }

    if (m_useTavily) {
        // Tavilyで開始
        TavilySearchProvider *tavily = new TavilySearchProvider(m_tavilyApiKey, this);
        connect(tavily, &ISearchProvider::searchFinished, this, &SearchManager::on_providerFinished);
        m_currentProvider = tavily;
        tavily->search(m_query);
    } else {
        // DuckDuckGoで開始 (またはTavily失敗時のフォールバック)
        DuckDuckGoSearchProvider *ddg = new DuckDuckGoSearchProvider(this);
        connect(ddg, &ISearchProvider::searchFinished, this, &SearchManager::on_providerFinished);
        m_currentProvider = ddg;
        ddg->search(m_query);
    }
}

void SearchManager::on_providerFinished(const QString &resultText, bool success) {
    if (success) {
        emit searchFinished(resultText, true);
    } else if (m_useTavily) {
        // Tavilyで失敗したため、DuckDuckGoに切り替えてフォールバック実行
        qWarning() << "Tavily search failed. Falling back to DuckDuckGo...";
        m_useTavily = false;
        startNextProvider();
    } else {
        // すべてのプロバイダが失敗した場合
        emit searchFinished("検索結果を取得できませんでした。", false);
    }
}

### 4.6 Twitch OAuth Implicit Flow におけるフラグメント転送ロジック

Twitch の Implicit Flow では、認可完了後にアクセストークンがURLのフラグメント（`#access_token=...`）としてブラウザに返却される。
ブラウザはフラグメント部分をHTTPリクエストとして直接サーバーへ送信しないため、ローカルHTTPサーバーはまずJavaScriptを含むレスポンスをブラウザへ返し、ブラウザ側でハッシュをパースさせ、再度 `/token` エンドポイントへクエリパラメータとして転送（リダイレクト）させる。

**ブラウザへ返却する HTML/JS レスポンス例:**
```html
<!DOCTYPE html>
<html>
<head>
    <title>Twitch Authentication</title>
    <script>
        window.onload = function() {
            var hash = window.location.hash;
            if (hash) {
                var params = new URLSearchParams(hash.substring(1));
                var token = params.get("access_token");
                if (token) {
                    // /token エンドポイントへクエリとして再送信
                    window.location.href = "/token?access_token=" + token;
                } else {
                    document.body.innerText = "Error: Access token not found in URL.";
                }
            } else {
                document.body.innerText = "Error: Hash fragment not found.";
            }
        };
    </script>
</head>
<body>
    Connecting to app... Please wait.
</body>
</html>
```

**一時サーバー側（/token 受信時）の処理フロー:**
1. `/token?access_token=...` のリクエストを受信したら、クエリパラメータからアクセストークンを抽出する。
2. 接続成功画面（例: 「認証が完了しました。アプリへお戻りください。」）をブラウザへ返す。
3. 受信したアクセストークンを local_settings.json に保存し、一時HTTPサーバーをシャットダウンする。
4. Twitch チャット接続（WebSocket）を開始する。

### 4.7 OBS連携用WebSocket・WebHookペイロード仕様

アバターの状態変更やAIの回答メッセージを外部（OBSブラウザソースや外部のWebHook連携先ツール）に通知する際、以下のJSONペイロードフォーマットを用いる。

#### A. アバター状態変化通知 (`avatar_changed`)
アバターの表情やポーズ状態が遷移した（例: 待機中 -> 思考中）際に配信される。
```json
{
  "event": "avatar_changed",
  "state": "thinking",
  "image": "thinking.png",
  "anchorX": 120,
  "anchorY": 182
}
```

#### B. AI応答通知 (`ai_response`)
AIの新しい回答テキストが生成され、右側吹き出しペインに表示されるタイミングで配信される。
```json
{
  "event": "ai_response",
  "text": "AIの回答テキストです。マークダウン形式が含まれる場合があります。"
}
```

#### C. WebSocket接続時の初期同期通知 (`init`)
OBSブラウザソース接続時等に、現在の最新状態を同期するための情報。
```json
{
  "event": "init",
  "state": "idle",
  "image": "idle.png",
  "anchorX": 120,
  "anchorY": 180,
  "last_response": "直近のAIの回答テキストです。"
}
```
```

### 4.8 AI入出力ブラックリストフィルタリングおよびマスク（伏字化）ロジック（ホワイトリスト対応）

不適切な入力がAIに渡されるのを防ぎ、またAIの不適切な応答（すり抜けによる応答や自発的な応答）がアバターを通じて出力されるのを防ぐため、`AIClientManager` は以下の処理フローを実行する。

1. **ブラックリスト・ホワイトリストのロード (`loadBlacklist`, `loadWhitelist`)**
   - 設定ファイル等から `blacklist_enabled` が `true` の場合、指定のディレクトリ優先順位から `blacklist.txt` および `whitelist.txt` を読み込む。
   - それぞれUTF-8でデコードし、1行につき1つの単語またはフレーズとして `m_blacklist` (QStringList) および `m_whitelist` (QStringList) に登録する。空行や `#` で始まるコメント行は除外する。

2. **マスクの適用 (`applyMask`)**
   - 与えられた文字列（`text`）に対して、以下の「プレースホルダー一時退避アルゴリズム」を用いてマスク化を行う。
     1. **ホワイトリストの一時退避**: `text` 内に含まれる `m_whitelist` の各単語・フレーズを検索し、大文字小文字を区別せず、一意のプレースホルダー（例: `__WHITE_LIST_PLACEHOLDER_N__`）に置換して退避させる（スペースを含んだフレーズもそのまま退避され、保護されます）。
     2. **ブラックリストの置換（マスク）**: 退避させた状態の文字列に対して、`m_blacklist` の各単語を大文字小文字を区別せず、一律で `****`（アスタリスク4文字）に置換する。
     3. **ホワイトリストの復元**: 退避させていたプレースホルダー部分を、元のホワイトリストの単語・フレーズに再置換して復元する。
   - これにより、`WTF` や `holy shit` などのホワイトリストで保護された表現の一部にブラックリストワードが含まれていたとしても、巻き添えで伏字化されるのを防止し、文脈（特定の単語の組み合わせ）に基づいた除外制御を実現する。

3. **入力（要求）のマスク制御フロー (`on_requestAI`)**
   - コアからリクエストが来たら、送信前プロンプトに `applyMask` を適用してマスク後のプロンプト（`filteredPrompt`）を作成する。
   - AIにはこのマスクしたプロンプトを送信する。ログおよびUI送信イベントもマスク後のプロンプトで発行される。

4. **出力（応答）のマスク制御フロー (`on_clientRequestFinished`)**
   - AIから返答を受信したら、その応答テキストに `applyMask` を適用し、マスク後の応答テキスト（`filteredResponse`）を作成する。
   - この `filteredResponse` を用いて、会話履歴への追加、難読化ログの保存、アバター発話イベント（UI用通知）の発行を行う。

### 4.9 翻訳コマンドのパースと制御

Twitchチャットなどのコメントから翻訳機能が要求された場合、`AIClientManager` は以下の処理フローを実行する。

1. **コマンドの判定とパース (`on_requestAI`)**
   - 入力されたプロンプトが `trans`（大文字小文字を区別しない）で始まっているかを判定する。
   - `trans` コマンドが検出された場合、直後の引数を解析する：
     - 空白で分割し、第一引数が「言語指示子」（例: `en`, `ja`, `english`, `日本語` など）に合致するかを `isLanguageIndicator` 関数で判定する。
     - 言語指示子であると判定された場合はその指示子をターゲット言語（例: `English`）にマップし、残りの文字列を翻訳対象とする。
     - 言語指示子ではない、または第一引数のみ（引数が1つ）の場合は、デフォルトで日本語（`Japanese`）をターゲット言語とし、全引数を翻訳対象とする。
   - 内部フラグ `m_isTranslationRequest` を `true` に設定する。

2. **APIリクエストの送信**
   - 翻訳対象のテキストおよびターゲット言語に基づいて、翻訳指示プロンプト（例: `Translate the following text to [言語]. Output ONLY the translation without any other text, explanations, or quotes.\n\nText:\n[テキスト]`）を動的構築する。
   - 会話履歴バッファ（`m_chatHistory`）およびセッションコンテキスト（`m_sessionContext`）を空にした状態で API クライアントに要求を委譲する。これにより、過去の会話やマスコット風キャラクター設定の口調に影響されるのを防止する。

3. **応答受信時のバイパス処理 (`on_clientRequestFinished`)**
   - `m_isTranslationRequest` が `true` の場合、以下の処理を行う：
     - フラグを `false` に戻す。
     - 得られた応答テキストを伏字マスク処理（`applyMask`）した上で、アバター発話イベント（UI用通知）を直接発行する。
     - 会話履歴バッファ（`m_chatHistory`）への追加、会話数カウント、難読化ログファイル（`chat_history.enc`）への保存をすべてスキップ（バイパス）する。これにより、翻訳処理によるメモリバッファの汚染を防止する。

### 4.10 ニックネーム自動登録と保留選別アルゴリズム

Twitchコメントや自己紹介を検知してAIが呼び出したツールコール（`update_nickname`）について、`AIClientManager::handleNicknameUpdateRequest` は以下の処理フローを実行し、登録か保留かを判定する。

1. **パラメータの正規化**:
   - ターゲットユーザー名（`target`）、提案されたニックネーム（`nickname`）をトリム（`trimmed`）し、ユーザー名は小文字に統一（`toLower`）する。
2. **申請者 (`m_currentRequester`）の特定**:
   - 直前に処理されたTwitchコメントイベントから抽出されたアカウント名が `m_currentRequester` に設定されている。
3. **配信主名 (`m_streamerName`）の特定**:
   - `local_settings.json` の `twitch_channel` の値を事前にロードし、小文字にして保持している。
4. **自動登録の判定**:
   - `m_currentRequester == target`（本人からの指示・自己紹介）または `m_currentRequester == m_streamerName`（配信主からの指示）の場合：
     - `user_names.json` 内の `users` セクションから該当ユーザーのエントリをロード（存在しなければ新規作成）する。
     - `preferred` フィールドに提案されたニックネームを設定し、`nicknames` の配列にその愛称が含まれていなければ追加する。
     - 変更後のJSONオブジェクトを `user_names.json` に同期保存（`saveUserNames`）し、`userNamesUpdated` シグナルを発火してUI表示を同期させる。
     - AIに対して `"Success: Nickname for <target> updated to <nickname>."` という成功メッセージを同期的に返却し、AIに対話へ反映させる。
5. **承認待ち保留の判定**:
   - 上記条件を満たさない（第三者による他人のニックネーム変更要求）場合：
     - `pending_requests` 配列の中に、同一の申請者・対象者・ニックネームのリクエストが既に存在しないか確認する。
     - 重複がなければ、申請オブジェクト `{ "requester": m_currentRequester, "target": target, "nickname": nickname, "timestamp": 現在時刻 }` を生成して `pending_requests` に追加する。
     - `user_names.json` を同期保存（`saveUserNames`）し、`userNamesUpdated` シグナルを発火してUI表示（保留中テーブル）を同期させる。
     - AIに対して `"Notification: The nickname update request has been submitted to the streamer for approval."` という通知メッセージを同期的に返却し、AIに対話へ反映させる。

### 4.11 優先呼び名/愛称インジェクション処理

`AIClientManager::on_requestAI` にて、Twitchからのコメントリクエストを受けた際、`user_names.json` のデータに基づいてAIに対するプロンプトを構築・注入する。

1. `user_names.json` から対象ユーザー（`user`）の設定を検索する。
2. 設定が存在する場合：
   - `preferred` が設定されていれば、`"必ず「〇〇さん、」または「〇〇、」と呼びかけてください。他の呼び方は使わず、この呼び方で統一してください。"` という指示文を構築する。
   - `preferred` が空で `nicknames` 配列が存在する場合、配列内の要素をランダムに1つ選択し、`"愛称「〇〇」を使って『〇〇さん、』や『〇〇ちゃん、』などと呼びかけてください。"` という指示文を構築する。
3. 自己紹介等の検知補強：
   - いずれの指示文においても、末尾に `"もし今回のコメントで新たな呼び方の変更指示や、「〇〇です」などの自己紹介・名乗りがあれば、その指示に従い、今後の対話でそれを反映してください。"` を付与して、AIにツールコール（`update_nickname`）の自発的な実行を促す。
4. この指示文（`systemInstructions`）をプロンプトの先頭に `[システム指示: ...]` として付与し、APIクライアントに要求を渡す。
5. コアやUIへの完了通知イベント `AppEvent` 内の `text` からは、この注入された指示部分を正規表現 `\\[システム指示:.*?\\]\\n*` で綺麗に除去して元の発言テキストに戻したものを設定する。これにより、UIや配信ソースに指示プロンプトが露出しないようにする。

### 4.12 Discordの独立入出力およびアバター非連動制御ロジック

Discord 経由の会話が配信（OBS）側の演出に干渉しないようにするため、入出力経路ごとに以下のルーティングおよびバイパス処理を行う。

1. **イベントの発生源判定 (`CoreModule::on_notify_events`)**:
   - `DiscordReader` がゲートウェイ経由でメッセージを受信すると、`AppEvent`（`type = EventType::DiscordMessageReceived`、`text = メッセージ本文`、`extraData["channel_id"] = チャンネルID`、`extraData["user_id"] = 送信者ID`）をコアに通知する。
   - コアはこれを受信した際、配信側（Twitch/STT）のフローと異なり、**`notifyEventToUI` による UI への中継（Thinking状態への変更指示など）を意図的にスキップ**し、直接 `AIClientManager` に要求（`requestAI`）を委譲する。
2. **履歴の送信元識別付き結合 (`AIClientManager::on_requestAI`)**:
   - 共通の会話履歴 `m_chatHistory` にユーザーのメッセージを追加する際、送信元を識別可能にするため、プレフィックスとして `[Discord] <ユーザー名>: <本文>` もしくは `[Twitch] <ユーザー名>: <本文>` のようにタグを付与して格納する。
   - AI（Mistral）はこのプレフィックス情報を解釈することで、現在どちらのインターフェースで会話が行われているかを判別して適切な文脈で返答を構築できる。
3. **返信先の判定と非連動送信 (`CoreModule::on_notify_events` - AIResponseReceived 時)**:
   - AIから回答（`AIResponseReceived`）が戻ってきた際、コアはイベントの付随メタデータから送信元を判定する。
   - 送信元が Discord（`channel_id` が設定されている）の場合：
     - **UIやOBSへの表示通知、およびTTS（音声読み上げ）処理をすべてバイパス**する。
     - 代わりに、`DiscordReader::on_requestDiscordSend(channelId, replyText)` スロットを直接呼び出して、REST API を経由して Discord の対象チャンネルへテキストメッセージとして返信する。
     - これにより、Discord側からは完全にテキストベースでボットが独立して返信しているように動作する。

### 4.13 長期記憶サマリ・詳細の分離保存と想起（RAG）アルゴリズム

セッションリセット時の対話記録のアーカイブ化、および現在の対話文脈に応じた過去ログの動的想起（ロード）を制御する。

1. **記憶のアーカイブ生成 (`AIClientManager::resetSession`)**:
   - セッションリセットがトリガーされると、現在の `m_chatHistory` を詳細ログオブジェクトとして JSON 構造化し、`log/archive/detail_session_<timestamp>.json` に保存する。
   - 同時に、AIに「これまでの対話の要約（サマリ）と、主要なトピックキーワード（3〜5個）を抽出してください」と指示する特別なAPIリクエストを投げる。
   - AIから要約テキストとキーワードが得られたら、メタデータ（`session_id`、会話開始〜終了日時範囲）を付与してサマリ JSON を構築し、`log/archive/summary_session_<timestamp>.json` に保存する。
   - 保存完了後、メモリ上の `m_chatHistory` をクリアする。
   - **階層マージ（メタサマリ生成）判定**:
     - 保存済みの未マージの個別サマリファイル（`summary_*.json`）が 10 件に達した場合、自動的に AI に対し「これらの10件の会話サマリを要約し、この期間全体の話題を網羅するメタサマリを1件作成してください」と依頼する。
     - 生成されたメタサマリを `meta_summary_<id>.json` に保存し、マージされた10件の個別サマリIDを `child_sessions` 配列に記録する。マージされた個別サマリファイルは `log/archive/archived_summaries/` フォルダへ移動し、通常のスキャンのロード対象外とする。
2. **想起トリガーの判定と階層スキャン (`AIClientManager::scanMemorySummaries`)**:
   - 新しい対話要求（`on_requestAI`）が来た際、ユーザーの発言テキストを解析する。
   - 「過去」「以前」「前に言った」などの想起関連キーワードが検出された場合、あるいは現在の発言内容と直近のやり取りから主要な名詞を抽出した際、ディスクスキャンを行う。
   - **第一段階スキャン (メタサマリ＆最新サマリ)**:
     - `log/archive/` ディレクトリ内のすべての `meta_summary_*.json` および未マージの `summary_*.json` のメタデータをロードする。
     - ユーザー発言キーワードと要約内容の関連度を判定する。
   - **第二段階スキャン (アーカイブサマリ)**:
     - 第一段階で `meta_summary_*.json` がヒットした場合、その `child_sessions` リストに記録されている個別サマリ（`log/archive/archived_summaries/summary_*.json`）のみを二次ロードして詳細スキャンし、最終的な合致セッションIDを特定する。
     - 第一段階で `summary_*.json` がヒットした場合は、そのままそのセッションIDを特定する。
3. **詳細ログの動的インジェクション (`AIClientManager::loadMemoryDetail`)**:
   - 関連する過去セッションIDが特定された場合、対応する `detail_*.json` をロードし、詳細対話履歴（過去ログ）を復元する。
   - ロードした過去ログから、関連する対話のターン（Q&Aペア）を最大3つ抽出し、想起コンテキスト（`m_recalledContext`）としてフォーマットする。
   - APIクライアント（Mistral）へのリクエスト時、システム指示プロンプトの直後に `[過去の関連会話の記憶: ...]` というタグでこの想起コンテキストを注入し、AIへ渡す。
   - 生成された応答の中で過去の会話を踏まえた返答が得られたら、この `m_recalledContext` は次のターンではクリアされ、メモリ肥大化を防止する。

### 4.14 GroqAIClient — Groq Cloud API クライアント設計

Groq は OpenAI 互換エンドポイントを提供するため、`CerebrasAIClient` と同構造で実装する。

#### A. クラス構造

```cpp
// src/ai/groq_ai_client.h
class GroqAIClient : public IAIClient {
    Q_OBJECT
private:
    QNetworkAccessManager *m_networkManager;
    QString m_apiKey;
    QString m_model;           // デフォルト: "llama-3.1-8b-instant"
    SearchManager *m_searchManager;
    QString m_pendingPrompt;
    QJsonArray m_pendingMessages;
    QString m_activeToolCallId;
    bool m_isToolCalling;

public:
    explicit GroqAIClient(QObject *parent = nullptr);
    void sendRequest(const QString &prompt,
                     const QList<QPair<QString,QString>> &history,
                     const QString &sessionContext,
                     const QString &systemInstruction) override;
    void setApiKey(const QString &apiKey) override;
    void setModel(const QString &model);
    void setTavilyApiKey(const QString &tavilyKey) override;
    QString clientId() const override { return "groq"; }
    ProviderStatus defaultStatus() const override;

private slots:
    void on_networkReplyFinished(QNetworkReply *reply);
    void on_searchFinished(const QString &resultText, bool success);
};
```

#### B. APIエンドポイントとモデル一覧

| 用途 | エンドポイント |
| :--- | :--- |
| Chat Completions | `https://api.groq.com/openai/v1/chat/completions` |
| モデル一覧（自動取得用） | `https://api.groq.com/openai/v1/models` |

| モデルID | 推奨用途 | RPM | RPD | TPM |
| :--- | :--- | ---: | ---: | ---: |
| `llama-3.1-8b-instant` | Manager AI（推奨） | 30 | 14,400 | 131,072 |
| `llama-3.3-70b-versatile` | Worker AI（高精度） | 30 | 14,400 | 6,000 |
| `gemma2-9b-it` | Worker AI（バランス） | 30 | 14,400 | 15,000 |

#### C. レスポンスヘッダー解析

毎 API コール完了時に `on_networkReplyFinished` 内で以下を解析し、`RateLimitTracker::updateFromReply()` を呼び出す:

```cpp
void parseRateLimitHeaders(QNetworkReply *reply, const QString &clientId) {
    auto get = [&](const QByteArray &key) {
        return reply->rawHeader(key).trimmed();
    };
    // 例: "x-ratelimit-remaining-requests" → rpmRemaining
    // 例: "x-ratelimit-reset-requests"     → nextResetAt (ISO8601 or seconds)
}
```

#### D. defaultStatus() 返却値

```cpp
ProviderStatus GroqAIClient::defaultStatus() const {
    ProviderStatus s;
    s.provider      = "groq";
    s.available     = true;
    s.rpmMax        = 30;
    s.rpmRemaining  = 30;
    s.rpdMax        = 14400;
    s.rpdRemaining  = 14400;
    s.tpmMax        = 131072;
    s.tpmRemaining  = 131072;
    s.contextWindow = 131072;
    s.toolCall      = true;
    s.supportsDiff  = false;
    s.cost          = 0.0;
    s.latencyMs     = 0;
    return s;
}
```

---

### 4.15 ProviderStatus 構造体と IAIClient インターフェース拡張

#### A. ProviderStatus 構造体

```cpp
// src/ai/provider_status.h
#pragma once
#include <QString>
#include <QDateTime>

struct ProviderStatus {
    QString   provider;         // "groq" / "cerebras" / "mistral" / "dummy"
    bool      available = true; // レートリミット未到達なら true

    int rpmMax       = 0;  int rpmRemaining  = 0;
    int rpdMax       = 0;  int rpdRemaining  = 0;
    int tpmMax       = 0;  int tpmRemaining  = 0;
    int tpdMax       = 0;  int tpdRemaining  = 0;

    int    contextWindow = 0;
    bool   toolCall      = false;
    bool   supportsDiff  = false;
    double cost          = 0.0;
    int    latencyMs     = 0;

    QDateTime nextResetAt; // 最短リセット時刻
};
```

#### B. IAIClient 拡張メソッド

```cpp
// src/ai/iai_client.h に追加
virtual QString clientId()              const = 0;
virtual ProviderStatus defaultStatus()  const = 0;
```

各サブクラスは `clientId()` で識別文字列を、`defaultStatus()` でデフォルトの制限値を返す。

---

### 4.16 RateLimitTracker — 使用量追跡と ProviderStatus 管理

#### A. クラス概要

```cpp
// src/ai/rate_limit_tracker.h
class RateLimitTracker {
public:
    // 起動時に全クライアントのデフォルト状態を登録
    void registerClient(const ProviderStatus &defaultStatus);

    // APIレスポンスヘッダーから残量を更新
    void updateFromReply(const QString &clientId, QNetworkReply *reply);

    // 手動設定値でMax値を上書き（UI設定反映用）
    void setMaxValues(const QString &clientId, const ProviderStatus &manual);

    // レイテンシを移動平均（直近5回）で更新
    void recordLatency(const QString &clientId, int elapsedMs);

    // 使用可能かチェック（rpmRemaining > 0 かつ rpdRemaining > 0）
    bool isAvailable(const QString &clientId) const;

    // 全クライアントが枯渇している場合、最短リセット時刻とその情報を返す
    struct ResetInfo { QDateTime resetAt; QString clientId; QString limitType; };
    ResetInfo earliestResetTime() const;

    // 「X分後に使用可能」メッセージを生成（AI呼び出しなし）
    QString formatWaitMessage(const ResetInfo &info) const;

    // 現在の ProviderStatus を取得（UI表示用）
    ProviderStatus statusOf(const QString &clientId) const;
    QList<ProviderStatus> allStatuses() const;

    // 日/週/月単位を永続化・読み込み
    void saveToFile(const QString &path) const;
    void loadFromFile(const QString &path);

private:
    QMap<QString, ProviderStatus> m_statuses;
    QMap<QString, QList<int>>     m_latencyHistory; // 直近5回

    void updateAvailable(const QString &clientId);
    static QDateTime parseResetHeader(const QByteArray &value);
};
```

#### B. updateFromReply() ヘッダー解析ロジック

```
ヘッダー "x-ratelimit-remaining-requests" → rpmRemaining
ヘッダー "x-ratelimit-limit-requests"     → rpmMax（≠0なら上書き）
ヘッダー "x-ratelimit-remaining-tokens"   → tpmRemaining
ヘッダー "x-ratelimit-limit-tokens"       → tpmMax
ヘッダー "x-ratelimit-reset-requests"     → nextResetAt（パース: ISO8601 or "Xs" 形式）
```

値が空・0の場合は既存値を維持（欠損プロバイダ対策）。

#### C. 永続化ファイル `log/usage_stats.json`

```json
{
  "groq": {
    "rpd_max": 14400, "rpd_remaining": 14250,
    "tpd_max": 500000, "tpd_remaining": 455000,
    "day_start": "2026-07-12T00:00:00Z"
  },
  "cerebras": { ... },
  "mistral":  { ... }
}
```

アプリ起動時に読み込み。`day_start` が当日以前なら該当フィールドをデフォルト値にリセット。
分/時単位の `rpmRemaining` / `tpmRemaining` はメモリのみ（アプリ起動時にデフォルト値に戻す）。

#### D. formatWaitMessage() 出力例

| 状況 | 出力文字列 |
| :--- | :--- |
| RPM制限 (2分後) | 「現在すべてのAIクライアントがレート制限に達しています。最短で**2分後**に使用可能になります（Groq RPM制限解除）。」 |
| RPD制限 (翌0時) | 「現在すべてのAIクライアントがレート制限に達しています。最短で**本日23:59**に使用可能になります（Cerebras RPD制限解除）。」 |

---

### 4.17 AIRouter — クライアント選択ロジック

#### A. クラス概要

```cpp
// src/ai/ai_router.h
enum class AIRole { Manager, Worker };

class AIRouter {
public:
    // 優先度順に isAvailable == true の最初のクライアントIDを返す
    // 全クライアントが unavailable なら空文字を返す
    QString selectClient(AIRole role,
                         const RateLimitTracker &tracker,
                         const QStringList &priorityOrder) const;
};
```

#### B. 優先度リスト（デフォルト設定）

```cpp
// Worker ロール
QStringList workerPriority = { "groq", "cerebras", "mistral", "dummy" };

// Manager ロール（UIで設定したプロバイダを先頭に）
QStringList managerPriority = { m_managerProviderId, "groq", "cerebras", "mistral" };
```

#### C. AIClientManager への組み込み

`on_requestAI()` の先頭（スラッシュコマンド判定後）に以下を挿入:

```cpp
// 1. Worker 選択
QString workerId = m_router.selectClient(AIRole::Worker, m_tracker, m_workerPriority);
if (workerId.isEmpty()) {
    // 全クライアント枯渇 → 待機メッセージ生成
    auto info = m_tracker.earliestResetTime();
    QString msg = m_tracker.formatWaitMessage(info);
    AppEvent ev; ev.type = EventType::AIResponseReceived; ev.text = msg;
    emit notifyEvent(ev);
    return;
}

// 2. 選択したクライアントで応答生成
IAIClient *client = m_clientMap[workerId];
client->sendRequest(prompt, history, sessionContext, systemInstruction);
// ※ 使用後に updateFromReply() で残量更新（レスポンスヘッダー解析）
```

#### D. Manager AI フォールバック（将来フェーズ準備）

現フェーズではManagerロールの処理はC++ロジックのみ（API呼び出しなし）。
将来フェーズで以下を追加:

```cpp
// Manager AI へ最小プロンプトを送信
QString managerId = m_router.selectClient(AIRole::Manager, m_tracker, m_managerPriority);
if (!managerId.isEmpty()) {
    QString miniPrompt = buildManagerPrompt(availableWorkers);
    m_managerClient->sendRequest(miniPrompt, {}, "", "");
    // Manager AI の応答 "use:groq" → workerId 決定
}

## 5. Discord 外部スケジュール API 連携機能詳細設計

Discord からの予定に関する問いかけに対して、外部 API から取得したスケジュールデータをインジェクション（RAG）する機能の詳細設計を示す。

### 5.1 クラス設計 of 拡張 (`AIClientManager`)

`AIClientManager` クラスに、以下のプライベートメソッドを追加する。

```cpp
// src/ai/ai_client_manager.h
private:
    /**
     * @brief 外部 API から指定されたカテゴリのスケジュール情報を取得・復号化する
     * @param category "work" (作業) または "stream" (配信)
     * @param startDate 取得開始日 (YYYY-MM-DD)
     * @param days 取得日数
     * @return 復号化された Markdown 形式の予定リストテキスト
     */
    QString fetchSchedules(const QString &category, const QDate &startDate, int days);

    /**
     * @brief work と stream の双方のスケジュールを取得し、LLMに与えるシステムコンテキストを構築する
     */
    QString getDiscordSchedulesContext();
```

### 5.2 APIリクエストと復号化ロジックの実装詳細

`fetchSchedules` 内で、以下の順序で HTTP 通信および難読化解除を行う。

1. **URLの組み立て**:
   `https://streamers-tool.sakura.ne.jp/TaskFlow/public/schedules.php` に対し、`QUrlQuery` を用いてパラメータ `category`, `start_date`, `days` を設定する。
2. **ブロッキング待機**:
   `QNetworkAccessManager` を使用し、非同期の `get()` を投げる。`QEventLoop` と `QTimer` を接続し、最大 5 秒間イベントループを実行して待機する。
3. **レスポンス解析**:
   `QJsonDocument` でレスポンス（JSON）を読み込み、配列 `data` をイテレートする。
4. **TransCipher 復号**:
   - 暗号化されたタイトル `title` (Base64形式の文字列) を取得。
   - `QByteArray::fromBase64` でデコード。
   - `CipherEngine::decrypt(encryptedData, "test_secret_key_12345")` を実行。
   - 復号が成功した場合にプレーンテキストのタイトルを取得。

### 5.3 自動インジェクションのトリガー仕様

`AIClientManager::on_requestAI` の対話コンテキスト生成フェーズにおいて、以下の条件を満たしたときに自動実行する。

- **トリガー条件**:
  - 送信元が Discord である場合（`!m_currentDiscordChannelId.isEmpty()` または `user` が `[Discord:` から始まる）。
  - 送信プロンプト（伏字化マスク適用後の文字列）に、以下のいずれかの部分一致ワードが含まれる：
    `予定`、`スケジュール`、`タスク`、`状況`、`進捗`、`配信`、`作業`、`schedule`、`task`、`work`、`stream`
- **インジェクション先**:
  - `getDiscordSchedulesContext()` で生成された Markdown テキストを、AI API に送信する system 命令（`additionalSystemPrompt` / `system` ロール）の末尾に結合して送信する。
```


## 6. システム固定自動応答機能詳細設計

AIを介さず、システム側でルールベースの即答を行うための設計を示す。

### 6.1 クラス設計 (`SystemResponseManager`)

固定応答ルールを管理・判定する独立したモジュールとして `SystemResponseManager` クラスを定義する。

```cpp
// src/ai/system_response_manager.h
#pragma once
#include <QObject>
#include <QString>

class SystemResponseManager : public QObject {
    Q_OBJECT
public:
    explicit SystemResponseManager(QObject *parent = nullptr);
    ~SystemResponseManager();

    /**
     * @brief 入力プロンプトを判定し、固定応答があればそれを返す。なければ空文字列を返す。
     * @param prompt ユーザーの入力メッセージ
     */
    QString processPrompt(const QString &prompt);
};
```

### 6.2 応答ルール仕様と実装詳細 (`system_response_manager.cpp`)

`processPrompt` メソッド内で、以下の応答ルールを処理する。

1. **バージョン情報**:
   - **判定方法 (複数ワード AND 条件)**:
     - 入力に `version`、`バージョン`、`ばーじょん`、`versioninfo` のいずれかが含まれる。
     - **かつ**、プロンプトがそれら単体であるか、または `アバター`、`君`、`あなた`、`アプリ`、`システム`、設定されたアバター名（`avatarName`）のいずれかが含まれる。
     - **かつ**、他の対象（マイクラ、Windowsなど無関係な名詞）を尋ねるものではないことを確認する。
   - **返答テキスト**: 自動生成された `version.h` 内の `PROJECT_VERSION` を利用し、`"現在のバージョンは v%1 です。"` という形式で文字列を構築して返却する。

2. **使用中AI情報の問い合わせ**:
   - **判定方法 (複数ワード AND 条件)**:
     - 入力に `ai`、`エーアイ`、`モデル`、`プロバイダ` のいずれかが含まれる。
     - **かつ**、`アバター`、`君`、`あなた`、`アプリ`、設定されたアバター名が含まれる。
     - **かつ**、`使っている`、`使用している`、`動いている`、`稼働している` などの使用を示すワードや、所有格（`のai` など）が含まれる。
     - **かつ**、ChatGPT や OpenAI など無関係な雑談ワードが排除されている。
   - **返答テキスト**: 現在稼働しているAIプロバイダ名（例: `"mistral"` -> `"Mistral AI"`, `"groq"` -> `"Groq"`, `"cerebras"` -> `"Cerebras"`, `"dummy"` -> `"ダミーAIクライアント"`) を構築し、`"現在稼働しているAIは %1 です。"` として返却する。

### 6.3 コアフローとの統合 (`AIClientManager` の拡張)

`AIClientManager` は `SystemResponseManager` のインスタンスをプライベートメンバとして保持する。

1. **初期化**:
   - `AIClientManager` のコンストラクタにて `m_systemResponseManager = new SystemResponseManager(this);` として初期化する。
2. **呼び出し制御**:
   - `AIClientManager::on_requestAI` にて、スラッシュコマンドの前処理の直後（通常のAI処理に入る前）に、`m_systemResponseManager->processPrompt(prompt, m_provider, m_avatarName)` を呼び出す。
   - 戻り値が空文字列でない場合：
     - 通常のAIリクエスト処理（履歴追加やAPIリクエスト）をすべてバイパスする。
     - 戻り値のテキストを格納した `AppEvent`（`type = EventType::AIResponseReceived`）を発火（`emit notifyEvent`）し、メソッドを即座に `return` する。
