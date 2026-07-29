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

### 14. 会話履歴ビューアクラス構造 (F-30: HistoryViewerDialog)

```cpp
// src/ui/history_viewer_dialog.h
#pragma once
#include <QDialog>
#include <QTextEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QList>
#include "ai/ai_client_manager.h"

class HistoryViewerDialog : public QDialog {
    Q_OBJECT
public:
    explicit HistoryViewerDialog(AIClientManager *aiManager, QWidget *parent = nullptr);
    ~HistoryViewerDialog() override = default;

private slots:
    void onPageSizeChanged(int index);
    void onPrevPage();
    void onNextPage();
    void onExportText();
    void onForceSummarize();

private:
    void updateView();

    AIClientManager *m_aiManager;
    QList<ConversationEntry> m_allEntries;
    int m_pageSize = 50;
    int m_currentPage = 1;

    QLabel *m_statusLabel;
    QTextEdit *m_textEdit;
    QComboBox *m_pageSizeCombo;
    QPushButton *m_prevButton;
    QPushButton *m_nextButton;
    QLabel *m_pageLabel;
    QPushButton *m_exportButton;
    QPushButton *m_summarizeButton;
};
```
    
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

### 3.2.5 AIRandomUtils (AI向けランダム値取得 I/F モジュール)

AIプロンプトやシステム文章内から呼び出せるランダム抽出ユーティリティ。

```cpp
#pragma once
#include <QString>
#include <QList>

namespace AIRandomUtils {
    /**
     * @brief min から max の閉区間 [min, max] で1つの整数をランダム抽選する。
     */
    int getRandom(int min, int max);

    /**
     * @brief 0 から max の閉区間 [0, max] から、重複しない整数を count 個取得する。
     */
    QList<int> getRandomList(int max, int count);

    /**
     * @brief 文字列内の "Random(min, max)" や "RandomList(max, count)" マクロ式を自動パース・評価・置換する。
     */
    QString parseAndEvaluate(const QString &text);
}
```

#### ロジック・アルゴリズム詳細
1. `getRandom(int min, int max)`:
   - `min > max` の場合は値を反転（スワップ）。
   - `QRandomGenerator::global()->bounded(min, max + 1)` を使用して公平に抽選。
2. `getRandomList(int max, int count)`:
   - `max < 0` または `count <= 0` の場合は空リストを返却。
   - 抽出候補数 `totalCount = max + 1` (0〜max)。
   - `count > totalCount` の場合は `count = totalCount` に自動クランプ。
   - 0〜maxのリストを生成し、`std::shuffle` または `QRandomGenerator` でシャッフル後、先頭 `count` 個を抽出して返す。
3. `parseAndEvaluate(const QString &text)`:
   - `QRegularExpression` で `Random\\((-?\\d+)\\s*,\\s*(-?\\d+)\\)` を抽出し、`getRandom` の戻り値文字列へ置換。
   - `QRegularExpression` で `RandomList\\((\\d+)\\s*,\\s*(\\d+)\\)` を抽出し、`getRandomList` のカンマ区切り文字列（例: `"1, 3, 9"`）へ置換。

---

### 3.2.6 TwitchReader Watchdog (サイレント切断探知・自動再接続モジュール)

Twitch IRC WebSocket コネクションの半開状態（サイレントドロップ）を常時監視・自己修復するタイマー機構。

#### アルゴリズム詳細
1. **タイムスタンプ更新**:
   - `TwitchReader::onTextMessageReceived()` にて、Twitch サーバーからデータ（`PING`, `PRIVMSG`, `JOIN`, `001` 等）を受信するたびに `m_lastDataReceivedTime = QDateTime::currentDateTime()` を最新日時へ更新。
2. **Watchdog 判定ルーチン (`checkWatchdog`)**:
   - 60秒周期でタイマーを発火し、`m_isRunning == true` かつ `m_webSocket` インスタンスが存在する場合に実行。
   - `m_lastDataReceivedTime.secsTo(QDateTime::currentDateTime()) >= 180` （3分以上無通信）の場合：
     - `qWarning() << "TwitchReader Watchdog: No data received for over 180 seconds. Connection seems lost. Auto-reconnecting...";`
     - 古い WebSocket オブジェクトのシグナルを切断して `deleteLater()`。
     - `connectToTwitch()` を呼び出し、`wss://irc-ws.chat.twitch.tv:443` へ自動再接続・再認証・JOIN を実行。

---

### 3.2.8 レイドクリエイター自動紹介・シャウトアウト・アナウンスルーティング仕様

レイド受信時にAI紹介文生成、`/announce` 枠付き投稿、Twitch公式 `/shoutout` コマンド、およびフォロー呼びかけコメントの送出を確実に制御するルーティング機構。

#### アルゴリズム詳細
1. **AI紹介文のアナウンス投稿 (`m_shoutoutUseAnnounce`)**:
   - レイド紹介文生成応答（`m_isShoutoutRequest == true`）時、`m_shoutoutUseAnnounce` が有効な場合、プレフィックスとして `/announce <color>` （指定色または 5色からランダム）を自動付与する。
2. **`TwitchReader` におけるコマンドそのまま送信**:
   - `TwitchReader::on_requestTwitchSend()` は `/announce` や `/shoutout` 等のスラッシュコマンドプレフィックスを文字消去せず、そのまま Twitch IRC サーバーへ `PRIVMSG #channel :<command>` として送信する。
3. **`/shoutout` イベントの確実なチャンネル宛てルーティング**:
   - `handleRaidShoutout` および `processNextShoutoutInQueue` で発行する `shoutoutEv` に `extraData["twitch_channel"]` を付与。
   - `CoreModule::on_notify_events` または `DirectInputSubmitted` / `AIResponseReceived` ルーティングにて `extraData` に `twitch_channel` が存在する場合、`requestTwitchSend` を確実に呼び出して Twitch チャットへコマンドを送信する。
4. **Twitch公式シャウトアウト成功連動**:
   - `TwitchReader` で `USERNOTICE` の `msg-id=shoutout_success` を検出した際、`ShoutoutSuccessReceived` イベントを発行。
   - `AIClientManager::on_shoutoutSuccessReceived` にて「フォロー呼びかけ」コメント（`/announce` 設定連動）を Twitch へ投稿する。

---

### 3.2.9 アバター共通・基本設定UI化およびOBS用アバターURL化仕様 (F-30)

設定画面 (`AvatarWindow::initSettingsTab`) におけるグループボックスの再統合と、OBS取り込み用表示のURL化・HTTP常時有効化の仕様。

#### グループボックス構造と配置エレメント
1. **「アバター共通・基本設定」グループボックス (`QGroupBox`)**:
   - **アバター名 (`m_avatarNameEdit`)**: QLineEdit
   - **アバタースキン (Skin & Builderボタン)**: `m_comboAvatarSkin` ＋ `m_btnSkinBuilder` ("新規作成 / 編集...")。※「OBS / 描画設定」から移動整合。
   - **名前反応 (`m_nameReactionCheckbox`)**: QCheckBox ("名前（アバター名）呼ばれて反応する")
   - **ウェイクワード / 判定 (`m_twitchWakeWordEdit` / `m_twitchWakeWordModeCombo`)**: QHBoxLayout 内にウェイクワード入力欄と判定コンボボックス (contains / prefix) を横並び配置。
2. **「OBS / 描画設定」グループボックス (スリム化 ＆ URL表記化)**:
   - WebSocket ポート (OBS用): `m_wsPortEdit`
   - HTTP配信ポート: `m_obsHttpPortEdit`
   - OBS用アバターURL: `http://localhost:<ポート>/avatar_obs.html` (表示更新 ＆ 「URLをコピー」ボタン)
   - 吹き出し表示秒数: 短 / 長
   - ※「OBS用HTTPサーバー有効化:」チェックボックスは廃止し常時起動。
3. **「Twitch 連携設定」グループボックス (スリム化)**:
   - チャンネル名 (`m_twitchChannelEdit`)
   - 起動時挨拶 (`m_twitchGreetingCheckbox`)
   - ※ クライアント ID および OAuth用ポートは UI から削除し、設定ファイル (`local_settings.json`) のみで保持・管理する。

---

### 3.2.10 TaskFlow(予定管理システム) 独立連携 ＆ 全プラットフォーム対応仕様 (F-31)

TaskFlow(予定管理システム) 連携設定を独立化し、Twitch チャット・Discord チャット・UI直接入力を問わず予定参照を可能とする仕様。

#### 設定構造とコンテキスト注入ロジック
1. **「TaskFlow(予定管理システム)連携設定」グループボックス (`QGroupBox`)**:
   - **連携有効化 (`m_taskFlowEnabledCheckbox`)**: "TaskFlow 連携を有効にする" チェックボックス。
   - **自由可変 API URL (`m_taskFlowApiUrlEdit`)**: ユーザー固有の TaskFlow schedules.php エンドポイントURL設定。
2. **全入力ソース共通の予定取得・プロンプトインジェクション**:
   - `AIClientManager::on_requestAI` にて、`m_taskFlowEnabled == true` かつプロンプト内に「予定」「スケジュール」「タスク」「進捗」「配信予定」等のキーワードが含まれる場合、`getTaskFlowSchedulesContext()` を呼び出す。
   - 入力元（Twitch / Discord / UI）に関わらず、TaskFlow API から今日〜7日間の作業・配信タスクを取得し、AIシステムプロンプトへコンテキストとして自動追加する。

---

### 3.2.11 新規 AI プロバイダ統合仕様 (F-32: HuggingFace / OpenRouter / さくらAI)

追加プロバイダ クラス（`HuggingFaceAIClient`, `OpenRouterAIClient`, `SakuraAIClient`）の詳細設計仕様。

#### 1. クラス構造とインターフェース継承
3 クラスともに `IAIClient` を継承し、OpenAI 互換 Chat Completions HTTP POST 通信を行う。

- **`HuggingFaceAIClient`**:
  - デフォルトエンドポイント: `https://router.huggingface.co/v1/chat/completions`
  - 動的モデル取得: `GET https://router.huggingface.co/v1/models` を呼び出し、アカウントで現在アクセス可能な対話用 `Instruct/Chat` モデルを全自動抽出し動的に適用。
  - リクエスト補正: `max_tokens: 1024`, `stream: false` を送信 JSON に明示付与。
  - 認証ヘッダー: `Authorization: Bearer <huggingface_api_key>`
- **`OpenRouterAIClient`**:
  - デフォルトエンドポイント: `https://openrouter.ai/api/v1/chat/completions`
  - 動的モデル取得: `GET https://openrouter.ai/api/v1/models` を呼び出し、現在利用可能な `:free` 無料枠または最良オープンモデルを全自動抽出し動的に適用。
  - 認証ヘッダー: `Authorization: Bearer <openrouter_api_key>`, `HTTP-Referer: https://github.com/BLUE000/AiAssistantAvatar`
- **`SakuraAIClient`**:
  - デフォルトエンドポイント: `https://api.sakura.io/v1/chat/completions`
  - デフォルトモデル: `sakura-llm`
  - 認証ヘッダー: `Authorization: Bearer <sakura_api_key>`

#### 2. UI 設定およびデータ永続化仕様 (`AvatarWindow` / `AIClientManager` / `local_settings.json`)
- **UI 設定項目**:
  - AIプロバイダー選択チェックボックス (`m_aiProviderHuggingFaceCheckbox`, `m_aiProviderOpenRouterCheckbox`, `m_aiProviderSakuraCheckbox`)
  - 各プロバイダ API キー入力欄 (`m_huggingfaceApiKeyEdit`, `m_openrouterApiKeyEdit`, `m_sakuraApiKeyEdit`)
  - 各プロバイダ Model 入力欄 (`m_huggingfaceModelEdit`, `m_openrouterModelEdit`, `m_sakuraModelEdit`)
- **JSON キー名**:
  - `ai_provider`: `"huggingface"` | `"openrouter"` | `"sakura"`
  - `huggingface_api_key`, `openrouter_api_key`, `sakura_api_key`
  - `huggingface_model`, `openrouter_model`, `sakura_model`
- **Save / Load 双方向同期および定数管理仕様**:
  - 設定保存 (`AvatarWindow::saveSettingsFromUI`) と設定ロード (`AIClientManager::loadSettingsFromJsonObject`) の両方において、上記 JSON キー名の対（ツイン）処理を保証する。
  - 各プロバイダのデフォルトモデル名はコード内への直書き（マジックストリング）を禁止し、`ConfigDefaults` 定数クラス（`DEFAULT_HUGGINGFACE_MODEL`, `DEFAULT_OPENROUTER_MODEL`, `DEFAULT_SAKURA_MODEL`）で一元管理する。

---

### 3.2.7 MarkdownTableEngine (マークダウン汎用データストレージ・抽出モジュール)

`knowledge/` ディレクトリ配下に保管された階層ドキュメント構造からテーブルレコードをスキャン・抽出するデータエンジン。

```cpp
#pragma once
#include <QString>
#include <QList>
#include <QMap>

struct TableRecord {
    QString group;                  // 情報グループ名 (例: "Elin")
    QString category;               // カテゴリ名 (例: "装備")
    QString tableName;              // テーブル名/ファイル名 (例: "片手剣")
    QStringList headers;            // カラム名ヘッダーリスト (例: ["武器名", "攻撃力", "必要素材"])
    QList<QMap<QString, QString>> rows; // データ行
};

class MarkdownTableEngine {
public:
    explicit MarkdownTableEngine(const QString &rootDir = "knowledge");

    // 全ナレッジファイルのロード＆インデックス化
    void reload();

    // 特定のキー検索によるカラム値抽出
    QString queryColumn(const QString &group, const QString &category, const QString &table, const QString &searchKey, const QString &targetColumn) const;

    // 特定テーブルからのランダム1件指定カラム抽出
    QString selectRandomColumn(const QString &group, const QString &category, const QString &table, const QString &targetColumn) const;

    // テキスト内の "TableSearch(...)" や "TableSelectRandom(...)" マクロ式を自動評価・置換
    QString parseAndEvaluate(const QString &text) const;

    // 自然文クエリから関連データ行を自動検索してAIプロンプト注入用コンテキスト文字列を生成
    QString searchRelevantContext(const QString &query) const;

private:
    QString m_rootDir;
    QList<TableRecord> m_tables;

    bool isPathSafe(const QString &path) const;
    void scanDirectory(const QString &dirPath, const QString &currentGroup, const QString &currentCategory);
    void parseMarkdownFile(const QString &filePath, const QString &group, const QString &category);
};
```

#### ロジック・アルゴリズム詳細
1. **サンドボックス境界チェック (`isPathSafe`)**:
   - `QDir::cleanPath` および `QFileInfo(filePath).canonicalFilePath()` により絶対パスを評価。
   - パスが `knowledge/` ルートディレクトリの配下に完全収まっているか検証し、`../` トラバーサルを防止。
2. **テーブル解析 (`parseMarkdownFile`)**:
   - 行読み込み時、`|` 文字で分割される行をテーブルとして認識。
   - ハイフン行（`|:---|:---|`）の直前行を `headers` とし、直後以降の行をデータマップ `QMap<QString, QString>` へ格納。
3. **クロステーブル・ランダム抽出 (`selectRandomColumn`)**:
   - 合致する `TableRecord` の全 `rows` から `QRandomGenerator::global()->bounded(rows.size())` で1件抽出し、指定 `targetColumn` の値を返却。
4. **マクロ式自動パース評価 (`parseAndEvaluate`)**:
   - `TableSearch("グループ", "カテゴリ", "テーブル", "検索キー", "対象カラム")` 
   - `TableSelectRandom("グループ", "カテゴリ", "テーブル", "対象カラム")`
   - 上記パターンを正規表現で検出・評価し、返却文字列に一括置換。

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
   - 設定ファイル等から `blacklist_enabled` が `true` の場合、実行ファイルと同階層の `Config/` フォルダ配下（`Config/blacklist.txt`, `Config/whitelist.txt`）を最優先とするディレクトリ優先順位から読み込む。
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

### 4.11 優先呼び名/愛称インジェクションおよびプラットフォームID対応付け処理 (F-34)

`AIClientManager::on_requestAI` にて、Twitch/Discordからのコメントリクエストを受けた際、送信元タグをパースしてユーザー名を正しく正規化した上で `user_names.json` のデータに基づいてAIに対するプロンプトを構築・注入する。

1. **送信元プレフィックスの抽出とユーザー名正規化**:
   - `user` パラメータから `[Discord:チャンネルID] ユーザー名`、`[Twitch:チャンネル名] ユーザー名`、および `[Twitch] ユーザー名`（チャンネル指定なしのデフォルト形式）をパースし、プレフィックスタグを除去して純粋なユーザー名（`cleanUser`）を抽出する。
   - 抽出した `cleanUser` を小文字化（`.toLower()`）した `userLower` を生成し、辞書検索用の標準キーとして使用する。
   - UIの入力欄やマイク音声認識（STT）からの発言などプレフィックスタグが存在しない場合は、自動的に「配信主（Streamer）」の発言として処理する。
2. **`user_names.json` からの対応付け検索と呼びかけプロンプト構築**:
   - 発言のあったプラットフォーム（Twitch または Discord）に応じ、`users` オブジェクトから `twitch_id == userLower` または `discord_id == userLower` または プロファイルキーが一致するエントリを検索する（※勝手な自動追加・自動紐づけは行わない）。
   - **優先呼び名（`preferred`）が設定されている場合**:
     - プラットフォームを問わず、`"必ず「〇〇さん、」または「〇〇、」と呼びかけてください。他の呼び方は使わず、この呼び方で統一してください。"` という指示文を構築する。
   - **優先呼び名が未設定（空）または未登録の場合**:
     - AIによる勝手なカタカナ推測変換を排除し、発言のあったプラットフォームのIDそのまま＋「さん」で呼びかける指示文を構築する：
     - Twitchからの発言時: `"冒頭で『cleanUserさん、』と呼びかけて回答してください。"`
     - Discordからの発言時: `"冒頭で『cleanUserさん、』と呼びかけて回答してください。"`
3. **`m_usersTable` 手動セル編集によるマージ（統合）アルゴリズム**:
   - `AvatarWindow` の「ニックネーム」管理タブ内のテーブル（5列構成: `{"優先呼び名", "Twitch ID", "Discord 名", "愛称リスト", "操作"}`）で、配信主が空欄のセル（例: `Discord 名`）に対応するID（例: `alice_discord`）を手動入力・確定した際、`AIClientManager::updateUserMapping` が発火する。
   - 入力されたID（`alice_discord`）を持つ既存の別レコードが存在するか検索する。
   - 存在するファイル/レコードを発見した場合、2つのレコードを1つのプロファイルに自動的にマージ（`mergeUserProfiles`）し、旧レコードを削除して `user_names.json` を同期保存・UIテーブルを再描画する。



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
## 7. 繝ｬ繧､繝峨・繧ｯ繝ｪ繧ｨ繧､繧ｿ繝ｼ閾ｪ蜍慕ｴｹ莉区ｩ溯・隧ｳ邏ｰ險ｭ險・(F-22)

### 7.1 險ｭ螳夂ｮ｡逅・ｻ墓ｧ・(local_settings.json 縺ｮ諡｡蠑ｵ)
local_settings.json 縺ｫ莉･荳九・險ｭ螳夐・岼繧定ｿｽ蜉縺励∬ｵｷ蜍墓凾縺ｫ AIClientManager 縺翫ｈ縺ｳ AvatarWindow 縺ｫ隱ｭ縺ｿ霎ｼ繧薙〒驕ｩ逕ｨ縺吶ｋ縲・
| 險ｭ螳壹く繝ｼ | 蝙・| 繝・ヵ繧ｩ繝ｫ繝亥､ | 隱ｬ譏・|
|---|---|---|---|
| "raid_auto_shoutout_enabled" | ool | 	rue | Twitch繝ｬ繧､繝牙女菫｡譎ゅ・閾ｪ蜍慕ｴｹ莉区ｩ溯・縺ｮ譛牙柑/辟｡蜉ｹ |
| "shoutout_conversation_enabled"| ool | 	rue | 閾ｪ辟ｶ險隱槭・蟇ｾ隧ｱ・井ｾ具ｼ壹後・・＆繧薙ｒ邏ｹ莉九＠縺ｦ縲搾ｼ峨↓繧医ｋ逋ｺ蜍輔・譛牙柑/辟｡蜉ｹ |
| "shoutout_use_command" | ool | 	rue | Twitch蜈ｬ蠑・/shoutout [username] 繧ｳ繝槭Φ繝芽・蜍墓兜遞ｿ縺ｮ譛牙柑/辟｡蜉ｹ |
| "shoutout_use_announce" | ool | 	rue | 繧｢繝翫え繝ｳ繧ｹ譫 (/announce) 縺ｧ縺ｮ繝√Ε繝・ヨ謚慕ｨｿ縺ｮ譛牙柑/辟｡蜉ｹ |
| "shoutout_announce_color" | string | "random" | 繧｢繝翫え繝ｳ繧ｹ繧ｫ繝ｩ繝ｼ ("normal", "blue", "green", "orange", "purple", "random") |
| "shoutout_length" | string | "standard" | 邏ｹ莉区枚縺ｮ髟ｷ縺輔ｒ謖・ｮ・("short": 1縲・譁・ "standard": 2縲・譁・ "detailed": 3縲・譁・ |
| "shoutout_tone" | string | "譏弱ｋ縺丞・豌励↑蜿｣隱ｿ縺ｧ・・ | AI縺檎ｴｹ莉区枚繧剃ｽ懈・縺吶ｋ髫帙・蜿｣隱ｿ繝ｻ繝医・繝ｳ縺ｮ繝励Ο繝ｳ繝励ヨ謖・ｮ・|
| "shoutout_prefix" | string | "縲舌Ξ繧､繝画─隰昴・ | 繝√Ε繝・ヨ謚慕ｨｿ譎ゅ↓譁・ｭ縺ｫ莉倅ｸ弱☆繧九・繝ｬ繝輔ぅ繝・け繧ｹ |

### 7.2 UI 險ｭ險・(AvatarWindow)
- **縲後Ξ繧､繝峨・邏ｹ莉九阪ち繝・(m_shoutoutTab)**:
  - QTabWidget 縺ｫ譁ｰ隕上ち繝悶後Ξ繧､繝峨・邏ｹ莉九阪ｒ霑ｽ蜉縺吶ｋ縲・  - **繧ｰ繝ｫ繝ｼ繝励・繝・け繧ｹ 1: 閾ｪ蜍慕ｴｹ莉九・蜍穂ｽ懆ｨｭ螳・*
    - m_raidAutoShoutoutCheckBox: 縲卦witch繝ｬ繧､繝牙女菫｡譎ゅ↓閾ｪ蜍輔〒邏ｹ莉九☆繧九・    - m_shoutoutConversationCheckBox: 縲御ｼ夊ｩｱ繝ｻ繝√Ε繝・ヨ縺ｧ縺ｮ邏ｹ莉玖ｦ∵ｱゑｼ医弱・・＆繧薙ｒ邏ｹ莉九＠縺ｦ縲冗ｭ会ｼ峨↓蜿榊ｿ懊☆繧九・    - m_shoutoutUseCommandCheckBox: 縲卦witch蜈ｬ蠑・/shoutout 繧ｳ繝槭Φ繝峨ｒ繝√Ε繝・ヨ縺ｫ謚慕ｨｿ縺吶ｋ縲・  - **繧ｰ繝ｫ繝ｼ繝励・繝・け繧ｹ 2: 謚慕ｨｿ繧ｹ繧ｿ繧､繝ｫ繝ｻ繝医・繝ｳ險ｭ螳・*
    - m_shoutoutUseAnnounceCheckBox: 縲後い繝翫え繝ｳ繧ｹ譫 (/announce) 縺ｧ濶ｲ莉倥″陦ｨ遉ｺ縺吶ｋ縲・    - m_shoutoutAnnounceColorCombo: 繧ｫ繝ｩ繝ｼ驕ｸ謚・(騾壼ｸｸ / 髱・/ 邱・/ 繧ｪ繝ｬ繝ｳ繧ｸ / 邏ｫ / 繝ｩ繝ｳ繝繝)
    - m_shoutoutLengthCombo: 邏ｹ莉区枚縺ｮ髟ｷ縺・(遏ｭ繧・/ 讓呎ｺ・/ 縺励▲縺九ｊ)
    - m_shoutoutToneEdit: 隱槫ｰｾ繝ｻ繝医・繝ｳ謖・､ｺ (QLineEdit)
    - m_shoutoutPrefixEdit: 繝励Ξ繝輔ぅ繝・け繧ｹ譁・ｭ怜・ (QLineEdit)
  - **繧ｰ繝ｫ繝ｼ繝励・繝・け繧ｹ 3: 繧ｯ繝ｼ繝ｫ繧ｿ繧､繝繧ｹ繝・・繧ｿ繧ｹ**
    - m_shoutoutCooldownLabel: /shoutout 繧ｳ繝槭Φ繝峨・繧ｯ繝ｼ繝ｫ繧ｿ繧､繝谿九ｊ遘呈焚繧定｡ｨ遉ｺ・医後け繝ｼ繝ｫ繧ｿ繧､繝: 貅門ｙ螳御ｺ・阪∪縺溘・縲後け繝ｼ繝ｫ繧ｿ繧､繝谿九ｊ: 45遘偵搾ｼ・    - UI逕ｨ繧ｿ繧､繝槭・ m_cooldownUiTimer (1遘貞捉譛・ 縺ｧ谿九ｊ遘呈焚繧偵き繧ｦ繝ｳ繝医ム繧ｦ繝ｳ謠冗判縲・
### 7.3 Twitch Helix API 騾｣謳ｺ & 繝ｦ繝ｼ繧ｶ繝ｼ諠・ｱ蜿門ｾ嶺ｻ墓ｧ・- **蜿嶺ｿ｡繧､繝吶Φ繝亥・逅・(TwitchReader)**:
  - Twitch 謗･邯壽凾縺ｫ CAP REQ :twitch.tv/tags twitch.tv/commands 繧定ｦ∵ｱゅ・  - 繝ｬ繧､繝牙女菫｡譎ゅ！RC USERNOTICE 縺ｮ msg-id=raid 繧ｿ繧ｰ縺九ｉ msg-param-displayName (縺ｾ縺溘・ login) 繧貞叙蠕励＠縲～AppEvent(EventType::TwitchRaidReceived, payload) 繧帝夂衍縲・- **Helix API 縺ｫ繧医ｋ蜈ｬ髢区ュ蝣ｱ蜿門ｾ・(AIClientManager / TwitchHelixClient)**:
  1. GET https://api.twitch.tv/helix/users?login={username}
     - 繝ｦ繝ｼ繧ｶ繝ｼID (id)縲∬・蟾ｱ邏ｹ莉区枚 (description) 繧貞叙蠕励・  2. GET https://api.twitch.tv/helix/channels?broadcaster_id={id}
     - 逶ｴ霑代・驟堺ｿ｡繧ｫ繝・ざ繝ｪ繝ｻ繧ｲ繝ｼ繝蜷・(game_name)縲・・菫｡繧ｿ繧､繝医Ν (	itle) 繧貞叙蠕励・  3. **蜷悟錐蛻･莠ｺ豺ｷ蜈･髦ｲ豁｢繝ｻURL迚ｹ螳夊ｧ｣譫・*:
     - description (Bio) 蜀・°繧画ｭ｣隕剰｡ｨ迴ｾ https?:\/\/(www\.)?(twitter\.com|x\.com|youtube\.com|youtu\.be)\/[a-zA-Z0-9_.-]+ 縺ｧ蜈ｬ蠑輯NS繝ｻYouTube繝√Ε繝ｳ繝阪ΝURL繧呈歓蜃ｺ縲・     - 荳咲｢ｺ螳溘↑Web繧ｭ繝ｼ繝ｯ繝ｼ繝画､懃ｴ｢縺ｯ陦後ｏ縺壹∵歓蜃ｺ縺輔ｌ縺溷・蠑酋RL縺ｮ讎りｦ√・繧ｿ繧､繝医Ν縺ｮ縺ｿ繧定ｧ｣譫舌＠縺ｦ繧ｯ繝ｪ繧ｨ繧､繧ｿ繝ｼ縺ｮ螻樊ｧ諠・ｱ繧堤ｲｾ蠎ｦ鬮倥￥陬懷ｮ後☆繧九・
### 7.4 AI繝励Ο繝ｳ繝励ヨ逕滓・ & 繧ｷ繝｣繧ｦ繝医い繧ｦ繝域枚逕滓・繝ｭ繧ｸ繝・け
- **繧ｷ繧ｹ繝・Β謖・､ｺ繝励Ο繝ｳ繝励ヨ讒区・**:
  AI繧ｯ繝ｩ繧､繧｢繝ｳ繝医∈縺ｮ繝励Ο繝ｳ繝励ヨ萓具ｼ・  `
  縺ゅ↑縺溘・驟堺ｿ｡繧｢繝舌ち繝ｼ縺ｧ縺吶ゅΞ繧､繝峨＠縺ｦ縺上ｌ縺溘け繝ｪ繧ｨ繧､繧ｿ繝ｼ縲鶏display_name}縲阪＆繧薙・鬲・鴨繧定ｦ冶・閠・↓邏ｹ莉九☆繧九さ繝｡繝ｳ繝医ｒ菴懈・縺励※縺上□縺輔＞縲・  縲舌け繝ｪ繧ｨ繧､繧ｿ繝ｼ諠・ｱ縲・  - Twitch ID / 陦ｨ遉ｺ蜷・ {username} / {display_name}
  - 閾ｪ蟾ｱ邏ｹ莉・(Bio): {bio}
  - 逶ｴ霑代・驟堺ｿ｡繧ｲ繝ｼ繝/繧ｫ繝・ざ繝ｪ: {game_name}
  - 驟堺ｿ｡繧ｿ繧､繝医Ν: {title}
  - 蜈ｬ蠑輯NS/螟夜Κ諠・ｱ: {sns_info}

  縲仙・蜉帶擅莉ｶ縲・  - 髟ｷ縺・ {shoutout_length} (short: 50譁・ｭ礼ｨ句ｺｦ, standard: 100譁・ｭ礼ｨ句ｺｦ, detailed: 150譁・ｭ礼ｨ句ｺｦ)
  - 繝医・繝ｳ繝ｻ蜿｣隱ｿ: {shoutout_tone}
  - 諢溯ｬ昴・豌玲戟縺｡繧定ｾｼ繧√▽縺､縲∫嶌謇九・驟堺ｿ｡繧定ｦ九↓陦後″縺溘￥縺ｪ繧九ｈ縺・↑譏弱ｋ縺・ｴｹ莉区枚縺ｫ縺励※縺上□縺輔＞縲・  `

### 7.5 繝√Ε繝・ヨ謚慕ｨｿ & /shoutout 繧ｳ繝槭Φ繝・/ 繧ｯ繝ｼ繝ｫ繧ｿ繧､繝蛻ｶ蠕｡
- **繝√Ε繝・ヨ謚慕ｨｿ莉墓ｧ・*:
  - 逕滓・縺輔ｌ縺溽ｴｹ莉九さ繝｡繝ｳ繝医↓ shoutout_prefix 繧剃ｻ倅ｸ弱・  - shoutout_use_announce 縺梧怏蜉ｹ縺ｪ蝣ｴ蜷茨ｼ・    - 繧ｫ繝ｩ繝ｼ縺・"random" 縺ｮ蝣ｴ蜷医・ ["blue", "green", "orange", "purple"] 縺九ｉ繝ｩ繝ｳ繝繝驕ｸ蜃ｺ縲・    - TwitchReader 邨檎罰縺ｧ /announce {color} {prefix} {generated_text} 繧帝∽ｿ｡縲・  - shoutout_use_announce 縺檎┌蜉ｹ縺ｪ蝣ｴ蜷医・騾壼ｸｸ繝√Ε繝・ヨ譁・→縺励※騾∽ｿ｡縲・- **/shoutout 繧ｳ繝槭Φ繝牙宛蠕｡ & 繧ｯ繝ｼ繝ｫ繧ｿ繧､繝邂｡逅・*:
  - shoutout_use_command 縺梧怏蜉ｹ縺九▽縲∝燕蝗槭・ /shoutout 逋ｺ陦後°繧・120 遘剃ｻ･荳顔ｵ碁℃縺励※縺・ｋ蝣ｴ蜷茨ｼ・    - TwitchReader 邨檎罰縺ｧ /shoutout {username} 繧帝∽ｿ｡縲・    - 120 遘偵・繧ｿ繧､繝槭・ m_shoutoutCooldownTimer 繧帝幕蟋九・  - 120 遘偵・繧ｯ繝ｼ繝ｫ繧ｿ繧､繝荳ｭ縺ｫ騾｣邯壹＠縺ｦ繝ｬ繧､繝・邏ｹ莉九′逋ｺ逕溘＠縺溷ｴ蜷茨ｼ・    - Twitch蜈ｬ蠑・/shoutout 繧ｳ繝槭Φ繝峨・騾∽ｿ｡縺ｯ繧ｹ繧ｭ繝・・・医お繝ｩ繝ｼ蝗樣∩・峨・    - AI縺ｫ繧医ｋ邏ｹ莉区枚遶縺ｮ繝√Ε繝・ヨ謚慕ｨｿ・・announce蜷ｫ繧・峨♀繧医・繧｢繝舌ち繝ｼ縺ｮ蜷ｹ縺榊・縺苓｡ｨ遉ｺ繝ｻTTS隱ｭ縺ｿ荳翫￡縺ｯ騾壼ｸｸ騾壹ｊ螳溯｡後・    - UI (m_shoutoutCooldownLabel) 縺ｫ谿九ｊ繧ｯ繝ｼ繝ｫ繧ｿ繧､繝繧定｡ｨ遉ｺ縲・
### 7.6 繧ｯ繝ｩ繧ｹ讒矩 & 繧､繝吶Φ繝磯壻ｿ｡莉墓ｧ・(AppEvent)
- **譁ｰ隕上う繝吶Φ繝医ち繧､繝・*:
  - EventType::TwitchRaidReceived: Twitch縺九ｉ縺ｮ繝ｬ繧､繝画､懃衍騾夂衍 (payload 縺ｫ繝ｦ繝ｼ繧ｶ繝ｼ蜷阪∬ｦ冶・閠・焚繧呈ｼ邏・
  - EventType::ShoutoutCooldownUpdated: 繧ｯ繝ｼ繝ｫ繧ｿ繧､繝谿九ｊ遘呈焚縺ｮ譖ｴ譁ｰ騾夂衍 (payload 縺ｫ谿九ｊ遘呈焚 int 繧呈ｼ邏・

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

## 5. 外部スケジュール API 連携機能詳細設計

Discord、またはチャット入力画面（直接入力・音声入力）からの予定に関する問いかけに対して、外部 API から取得したスケジュールデータをインジェクション（RAG）する機能の詳細設計を示す。

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
  - 送信元が Discord または UI直接入力・音声入力である場合（`user` が `[Twitch` から始まらないリクエスト、Twitchは除外）。
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
   - **判定方法 (修飾語によるコンテキスト判定)**:
     - アバターへの言及、あるいは単体での `version` 入力を検知。
     - 入力に含まれる「〇〇のバージョン」というフレーズの修飾語（〇〇の部分）を解析し、それがアバター自身（`アバター`、`君`、`あなた`、`本体`、設定されたアバター名など）以外の具体的な名詞（例：マイクラ、Windows等）である場合は、他者のバージョンへの質問とみなし自動応答をバイパスする。
     - 修飾名詞が存在しない、またはアバター自身を指している場合のみ、固定自動応答を返却する。
   - **返答テキスト**: 自動生成された `version.h` 内の `PROJECT_VERSION` を利用し、`"現在のバージョンは v%1 です。"` という形式で文字列を構築して返却する。

2. **使用中AI情報の問い合わせ**:
   - **判定方法 (修飾語によるコンテキスト判定)**:
     - アバターへの言及がある状態で、`ai` や `モデル` などのキーワードが含まれていることを検知。
     - 「〇〇のAI」の修飾語を解析し、自分以外の名詞である場合は自動応答の対象外とする。
     - また、`使っている` や `稼働している` などの接続動詞が存在することを確認し、他社サービス（ChatGPTなど）に関する雑談文脈でなければ、固定応答を返却する。
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

## 7. レイド・クリエイター自動紹介機能詳細設計 (F-22)

Twitchレイド（Raid）受信時、またはコマンド/自然言語での要求発生時に相手クリエイター情報を非同期で収集し、AIによる紹介テキスト生成・投稿・`/shoutout` コマンド制御・GUI表示を行う設計の詳細。

### 7.1 設定仕様 (`local_settings.json`)
- `raid_auto_shoutout_enabled` (bool, デフォルト: true): レイド受信時の自動紹介ON/OFF
- `shoutout_conversation_enabled` (bool, デフォルト: true): 会話検知("〇〇さん紹介して"等)による紹介ON/OFF
- `shoutout_use_command` (bool, デフォルト: true): `/shoutout` コマンド送信機能のON/OFF
- `shoutout_follow_msg_enabled` (bool, デフォルト: true): `/shoutout` 成功時フォロー呼びかけ投稿のON/OFF
- `shoutout_follow_msg_template` (string, デフォルト: `"ぜひ {name} さんをフォローしてね！"`): フォロー呼びかけテンプレートテキスト
- `shoutout_use_announce` (bool, デフォルト: true): `/announce` 色付き枠投稿のON/OFF
- `shoutout_announce_color` (string, デフォルト: `"random"`): アナウンスカラー ("primary", "blue", "green", "orange", "purple", "random")
- `shoutout_length` (string, デフォルト: `"standard"`): 紹介文の長さ ("short", "standard", "detailed")
- `shoutout_tone` (string, デフォルト: `"明るく元気な口調で！"`): 紹介のトーン・口調

### 7.2 `/shoutout` 成功時フォロー呼びかけ処理詳細
- `TwitchReader` は Twitch チャット IRC の `NOTICE` / `USERNOTICE` メッセージから `msg-id=shoutout_success` を検知した際、`EventType::ShoutoutSuccessReceived` を発火する。
- `AIClientManager` は本イベントを受信した際、`shoutout_follow_msg_enabled` が `true` であれば、`shoutout_follow_msg_template` 内の `{name}` を対象ユーザーの表示名に置換し、`/announce` または通常チャットメッセージとして自動追加投稿を行う。

## 8. アバタースキン切替詳細設計 (F-23)

### 8.1 設定仕様 (`local_settings.json`)
- `avatar_skin` (string, デフォルト: `"FishEatCatSkin"`): 選択されたスキンフォルダ名。

### 8.2 UI制御 (`AvatarWindow`)
- 「設定」タブ内に「アバタースキン (Skin):」 `QComboBox` (`m_comboAvatarSkin`) を配置。
- 画面表示時、`QDir("pic")` の `entryList(QDir::Dirs | QDir::NoDotAndDotDot)` を実行し、存在するフォルダ名を `QComboBox` の選択肢に追加。
- スキン変更検知時:
  1. `local_settings.json` の `"avatar_skin"` に選択値を保存。
  2. アバター描画エンジンの読み込みルートパスを `pic/[SkinName]` に更新。
  3. `ObsHttpServer` の Document Root を `pic/[SkinName]` に更新し、OBSブラウザソースの表示スキンをアプリUIと完全同期・一括変更。

## 9. アバター画像指定3モード ＆ 状態タイマー詳細設計 (F-24)

### 9.1 `avatar_settings.json` 仕様構造
```json
{
    "idle": {
        "interval_ms": 15000,
        "front": { "mode": "single", "file": "Front01.png", "anchorX": 100, "anchorY": 100, "transparentX": 0, "transparentY": 0 },
        "back":  { "mode": "random", "files": ["Back01.png", "Back02.png"], "anchorX": 100, "anchorY": 100, "transparentX": 0, "transparentY": 0 },
        "right": { "mode": "sequence", "frame_interval_ms": 100, "files": ["Right01.png", "Right02.png"], "anchorX": 100, "anchorY": 100, "transparentX": 0, "transparentY": 0 },
        "left":  { "mode": "single", "file": "Left01.png", "anchorX": 100, "anchorY": 100, "transparentX": 0, "transparentY": 0 }
    },
    "listening": { "duration_ms": 500, "mode": "single", "file": "Front03.png", "anchorX": 100, "anchorY": 100, "transparentX": 0, "transparentY": 0 },
    "thinking":  { "duration_ms": 800, "mode": "single", "file": "Thinking01.png", "anchorX": 100, "anchorY": 100, "transparentX": 0, "transparentY": 0 },
    "speaking":  { "duration_ms": 2000, "mode": "sequence", "frame_interval_ms": 120, "files": ["Front01.png", "Front05.png"], "anchorX": 100, "anchorY": 100, "transparentX": 0, "transparentY": 0 }
}
```

### 9.2 アニメーション＆タイマー再生ロジック
- **`mode` 判定**:
  - `single`: 単一画像ファイルを表示。
  - `random`: 配列 `files` から `QRandomGenerator` で1枚をランダムに選出し表示。
  - `sequence`: `QTimer` で `frame_interval_ms` ごとに `files` のインデックスを進めてパラパラアニメーション表示（ループ指定可）。
- **タイマー制御**:
  - イベント受信時（入力受領時 `Listening` / AI処理時 `Thinking` / 応答時 `Speaking`）に該当状態の `duration_ms` タイマーを起動し、タイマー完了時に自動的に `Idle`（Front/Back/Right/Left 抽選表示）へ復帰。

## 10. アバタースキン自動生成・GUI編集詳細設計 (F-25)

### 10.1 GUI構成クラス (`AvatarSkinBuilderDialog`)
- **UIレイアウト**:
  - スキン名入力フィールド (`QLineEdit`)。
  - 各状態タブ/セクション（`Idle-Front`, `Idle-Back`, `Idle-Right`, `Idle-Left`, `Listening`, `Thinking`, `Speaking`）。
  - モード選択 (`QComboBox`: `single` / `random` / `sequence`)。
  - 画像ファイル一覧リスト (`QListWidget` + 「追加」「削除」ボタン)。
  - 各種パラメータ（`frame_interval_ms`, `duration_ms`, `anchorX`, `anchorY`, `transparentX`, `transparentY`）。
  - リアルタイムプレビュー描画領域 (`QLabel` + 十字アンカー表示描画)。

### 10.2 自動生成・ファイル入出力ロジック
- **ファイルコピー**:
  - 選択された外部画像を `QFile::copy` で `pic/[SkinName]/` ディレクトリ内へ適切なファイル名（例: `Front01.png`, `Listening.png` 等）で保存・配置。
- **`avatar_settings.json` の書き出し**:
  - GUI上で設定された項目から `QJsonObject` を組み立て、`QJsonDocument::toJson()` で `pic/[SkinName]/avatar_settings.json` に上書き書き出し。
- **`avatar_obs.html` の生成**:
  - アプリ組み込みの標準テンプレート文字列を `pic/[SkinName]/avatar_obs.html` に出力作成。

## 11. 多層スコア判定・文脈保護・中立検索連携フィルタリング詳細設計 (F-26)

### 11.1 設定ファイル仕様
- **`blacklist.txt` スキーマ**: `[単語または正規表現] , [カテゴリ] , [加算スコア]`
  - 例: `覚醒剤, drug, 40`, `作り方を教えて, instruction, 50`, `殺人, violence, 30`
- **`whitelist.txt` スキーマ**: `[単語または正規表現] , [補正カテゴリ] , [減算スコア]`
  - 例: `Elin, game_context, 40`, `RimWorld, game_context, 40`, `死ぬほど, emotion_context, 40`

### 11.2 モジュール設計 (`ScoreModerationEngine`)
- **`ScoreResult evaluate(const QString &inputText, const QList<QString> &historyTexts)`**:
  1. `blacklist.txt` をスキャンし、一致したカテゴリ・スコアを加算。`instruction` / `personal_info` フラグをチェック。
  2. `whitelist.txt` をスキャンし、一致した文脈保護スコアを減算。
  3. 直近会話履歴 `historyTexts`（過去3件以内かつ3分以内）から `history_context` の存在を検証。
     - **Jailbreak Guard**: `instruction` または `personal_info` フラグが `true` の場合、`history_context` の減算適用をキャンセル（0固定）。
## 12. 新規 AI プロバイダ統合 ＆ モデル指定UIプルダウン化詳細設計 (F-32)

### 12.1 プロバイダ別設定仕様 (`local_settings.json`)
- `ai_provider_huggingface` (bool, デフォルト: false): HuggingFace 使用ON/OFF
- `ai_provider_huggingface_key` (string): HuggingFace API Token
- `ai_provider_huggingface_model` (string, デフォルト: `"meta-llama/Llama-3.1-8B-Instruct"`): 使用モデル名
- `ai_provider_openrouter` (bool, デフォルト: false): OpenRouter 使用ON/OFF
- `ai_provider_openrouter_key` (string): OpenRouter API Key
- `ai_provider_openrouter_model` (string, デフォルト: `"meta-llama/llama-3.1-8b-instruct:free"`): 使用モデル名
- `ai_provider_sakura` (bool, デフォルト: false): さくらAI 使用ON/OFF
- `ai_provider_sakura_key` (string): さくらAI API Key (アカウントトークン: <UUID>:<シークレット>)
- `ai_provider_sakura_model` (string, デフォルト: `"llm-jp-3.1-8x13b-instruct4"`): 使用モデル名 (エンドポイント: https://api.ai.sakura.ad.jp/v1/chat/completions)

### 12.2 GUI制御・モデルプルダウン (QComboBox setEditable(true)) 仕様
- **HuggingFace モデルプルダウン選択肢**:
  1. `meta-llama/Llama-3.1-8B-Instruct` (推奨・高速)
  2. `Qwen/Qwen2.5-7B-Instruct` (日本語おすすめ)
  3. `Qwen/Qwen2.5-72B-Instruct` (高性能)
  4. `mistralai/Mistral-7B-Instruct-v0.3`
- **OpenRouter モデルプルダウン選択肢**:
  1. `meta-llama/llama-3.1-8b-instruct:free` (推奨・無料)
  2. `google/gemma-4-31b-it:free` (無料)
  3. `mistralai/mistral-7b-instruct:free` (無料)
  4. `qwen/qwen-2.5-72b-instruct`
- **さくらAI 公式最新モデルプルダウン選択肢**:
  1. `llm-jp-3.1-8x13b-instruct4` (国産LLM・推奨)
  2. `gpt-oss-120b`
  3. `preview/gemma-4-31B-it`
  4. `preview/Kimi-K2.6`
  5. `preview/Phi-4-mini-instruct-cpu`
  6. `preview/Phi-4-multimodal-instruct`
  7. `preview/Qwen3-0.6B-cpu`
  8. `preview/Qwen3-VL-30B-A3B-Instruct`
  9. `preview/Qwen3.6-35B-A3B`

### 12.3 エラーハンドリング・フォールバックルーティング仕様 (F-33)

#### 12.3.1 フォールバック対象エラーの分類

| HTTP コード | 種別 | 動作 |
|---|---|---|
| `429` | 一時的レート制限 | **フォールバック** → 次の利用可能プロバイダへ再送 |
| `503` | 一時的サービス不可 | **フォールバック** → 次の利用可能プロバイダへ再送 |
| `400` | 不正リクエスト | フォールバックなし・即時エラー通知 |
| `401` | 認証失敗 | フォールバックなし・即時エラー通知 |
| `403` | 権限不足 | フォールバックなし・即時エラー通知 |
| `404` | リソース未存在 | フォールバックなし・即時エラー通知 |

#### 12.3.2 フォールバック動作

- `AIClientManager::on_clientRequestFinished()` にてエラーコードを判定し、`429` / `503` の場合は元プロンプト（`m_pendingPrompt`）を保持したまま、API キーが設定済みの次のプロバイダへ再送する。
- フォールバック候補リストは `m_fallbackProviders`（`QStringList`）として `loadSettingsFromJsonObject()` 時に API キー設定済みプロバイダを登録順に構築する（選択中プロバイダ自身は除外する）。
- 全プロバイダがフォールバック済みで全て失敗した場合のみ最終エラーとして処理を終了する。
- Discord / Twitch チャンネルへはエラーを送出しない（フォールバックで対処し、視聴者に無応答を見せない）。

#### 12.3.3 UI メッセージ仕様（自然言語通知）

エラー種別ごとの UI（吹き出し）表示メッセージ。生の HTTP ステータスコードだけを表示しない。

```
429 フォールバック成功: ⚠️ [プロバイダ名] がレート制限中のため、[次プロバイダ名] に自動切り替えしました
429 全プロバイダ失敗:   ❌ 全ての AI プロバイダがレート制限中です。しばらく待ってから再試行してください
404:                     ❌ 指定したモデル名が見つかりません。AI設定タブでモデル名を確認してください
401:                     ❌ API キーが正しくありません。AI設定タブでキーを確認してください
下流 Provider 429:       ⚠️ [Provider名] がレート制限中です（OpenRouter 経由）。別プロバイダへ切り替えます
```

実装上は `AIClientManager::buildHumanReadableError(int httpCode, const QJsonObject &errorJson)` として抽出し、UI へのシグナルには自然言語文字列を渡す。

#### 12.3.4 ログ出力仕様（詳細ログ）

ログ（`qWarning`）へはレスポンス JSON の生エラー詳細を全文出力する（UI メッセージとは完全に分離）。

```
[AIClientManager] HTTP Error 429 from openrouter
  error.message    : Provider returned error
  provider_name    : Google AI Studio
  provider_code    : 429
  raw              : google/gemma-4-31b-it:free is temporarily rate-limited upstream...
```

#### 12.3.5 恒久エラー（旧 12.3 非フォールバック規則の継承）

- `400`, `401`, `403`, `404` 等の恒久エラーでは他プロバイダのエラーにすり替えず、当該プロバイダが返したエラー情報をそのままログ・UI へ通知する（設定ミスの隠蔽防止）。

- **疑似ファンクション呼び出しタグの全消去フィルター**:
  - `on_clientRequestFinished()` において、AI応答に含まれる `<function=...></function>` タグを正規表現で検出して裏で実行し、応答テキストからは完全消去して綺麗な会話本文のみを出力する。


### 13. ナレッジベース拡張詳細設計 (F-29)

#### 13.1 `knowledge_index.json` スキーマ仕様
```json
{
  "version": "1.0",
  "last_updated": "2026-07-26T05:00:00Z",
  "triggers": {
    "占い": [
      {
        "file_path": "knowledge/エンタメ/占い/星座占い.md",
        "title": "今日の星座占い",
        "priority": 100,
        "mode": "random_row",
        "columns": ["運勢", "幸運のアイテム", "ラッキーカラー", "アドバイス"],
        "status": "valid"
      }
    ]
  },
  "diagnostics": [
    {
      "file_path": "knowledge/テスト/broken.md",
      "line_number": 15,
      "error_type": "column_mismatch",
      "message": "テーブルの列数が一致しません (期待値: 4, 検出値: 3)",
      "timestamp": "2026-07-26T05:00:00Z"
    }
  ]
}
```

#### 13.2 `MarkdownTableEngine` データ構造の拡張
```cpp
struct KnowledgeIndexEntry {
    QString filePath;
    QString title;
    int priority = 100;
    QString mode; // "random_row", "table_search" 等
    QStringList triggers;
    QStringList columns;
    bool isValid = true;
    QString errorMessage;
    int errorLine = 0;
};

class MarkdownTableEngine {
public:
    // インデックスの再構築とエラーバリデーション
    bool buildIndexAndValidate(QJsonObject &outIndexData, QList<KnowledgeIndexEntry> &diagnostics);
    
    // トリガー一致＆優先度解決
    KnowledgeIndexEntry resolveBestEntryForTrigger(const QString &triggerWord) const;
};
```

