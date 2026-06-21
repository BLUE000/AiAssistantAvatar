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
    void requestAI(const QString &prompt);
    void requestSessionReset(bool isManual);
    void requestSessionImport(const QString &filePath);
    void requestSessionExport(const QString &encPath, const QString &txtPath);
    void settingsUpdated();
    void requestTwitchReauth();

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
    
    // 設定タブ用UI
    QLineEdit *m_wsPortEdit = nullptr;
    QLineEdit *m_twitchChannelEdit = nullptr;
    QLineEdit *m_twitchClientIdEdit = nullptr;
    QLineEdit *m_twitchClientSecretEdit = nullptr;
    QLineEdit *m_twitchPortEdit = nullptr;
    QLineEdit *m_twitchWakeWordEdit = nullptr;
    QComboBox *m_twitchWakeWordModeCombo = nullptr;
    QComboBox *m_aiProviderCombo = nullptr;
    QLineEdit *m_aiApiKeyEdit = nullptr;
    QLineEdit *m_webhookUrlEdit = nullptr;
    QCheckBox *m_webhookEnabledCheckbox = nullptr;
    
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
    
    void loadSettings();
    void processAndCacheImages();
    QPixmap applyTransparency(const QString &filePath, int tx, int ty);
    void updateAvatarDisplay(const QString &state);
    void updateWindowPosition();
    void showContextMenu(const QPoint &globalPos);
    
    void initSettingsTab(QWidget *parent);
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

public slots:
    // コアから通知を受け取るスロット
    void on_notify_events(const AppEvent &event);
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
    QString m_refreshToken;     // リフレッシュトークン
    QString m_clientId;
    QString m_clientSecret;     // クライアントシークレット
    QString m_wakeWord;
    QString m_wakeWordMode;     // "contains" または "prefix" / "command"
    int m_authPort = 48080;

    QWebSocket *m_webSocket = nullptr;
    QTcpServer *m_authServer = nullptr;
    QNetworkAccessManager *m_networkManager = nullptr;
    QString m_configPath;

    void loadSettings();
    void saveTokenToSettings(const QString &accessToken, const QString &refreshToken);
    void saveOAuthDataToSettings(const QString &accessToken, const QString &refreshToken, const QString &channel);
    void fetchChannelName(const QString &token);
    void startOAuthServer();
    void connectToTwitch();
    
    // 認可コード・リフレッシュトークン処理用
    void refreshTwitchToken();
    void requestTokensWithCode(const QString &code);

public:
    explicit TwitchReader(QObject *parent = nullptr);
    ~TwitchReader();

    void setSettings(const QString &channel, const QString &token, const QString &clientId, const QString &clientSecret, const QString &wakeWord);
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
    void onTokenRequestFinished(QNetworkReply *reply);
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
    virtual void sendRequest(const QString &prompt, const QList<QPair<QString, QString>> &history, const QString &sessionContext = QString()) = 0;
    virtual void setApiKey(const QString &apiKey) = 0;

signals:
    void requestFinished(const QString &responseText, bool success);
};
```

#### B. `AIClientManager` クラス (1段目)
コアモジュールからの通信および共通エラー/タイムアウト、イベント成型を担当する。
```cpp
#pragma once
#include <QObject>
#include "iai_client.h"
#include "app_event.h"

class AIClientManager : public QObject {
    Q_OBJECT
private:
    IAIClient *m_currentClient = nullptr;
    QString m_apiKey;
    QString m_transCipherKey;
    QList<QPair<QString, QString>> m_chatHistory; // ユーザー、AIの対話ペア
    int m_maxHistoryCount = 10; // 自動リセット契機（10件＝5往復）
    QString m_sessionContext; // マークダウンのコンテキスト情報
    bool m_isResetting = false; // 要約要求中かどうかのフラグ
    bool m_isManualReset = false; // 手動リセット中かどうかのフラグ
    QString m_lastPrompt; // 前回のプロンプト

    void loadCredentials();
    void loadSessionContext();
    void saveSessionContext(const QString &context);
    void saveObfuscatedLog(const QString &logText);
    QList<QPair<QString, QString>> loadObfuscatedBackup(const QString &filePath);

public:
    explicit AIClientManager(QObject *parent = nullptr);
    ~AIClientManager();
    void setAIProvider(const QString &provider); // "mistral" or "dummy"
    QList<QPair<QString, QString>> getChatHistory() const;

signals:
    void notifyEvent(const AppEvent &event);
    void chatHistoryUpdated(const QList<QPair<QString, QString>> &history);

public slots:
    void on_requestAI(const QString &prompt);
    void on_clientRequestFinished(const QString &responseText, bool success);
    void resetSession(bool isManual);
    bool importSessionBackup(const QString &filePath);
    void exportSessionBackup(const QString &encPath, const QString &txtPath);
    void on_requestChatHistory();
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
    void sendRequest(const QString &prompt, const QList<QPair<QString, QString>> &history = {}, const QString &sessionContext = QString()) override;
    void setApiKey(const QString &apiKey) override;
    void setTavilyApiKey(const QString &tavilyKey);

private slots:
    void on_networkReplyFinished(QNetworkReply *reply);
    void on_searchFinished(const QString &resultText, bool success);
};
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

### 4.6 OBS連携用WebSocket・WebHookペイロード仕様

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
