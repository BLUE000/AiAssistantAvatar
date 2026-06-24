# 基本設計書 - AI Assistant Avatar

## 1. システム構成とモジュール設計
本システムは Qt6 C++ フレームワークを使用し、UI（画面表示）とビジネスロジックを分離した構造を持つ。スレッド安全性と拡張性を確保するため、全体を以下のモジュールに分割する。

```mermaid
graph TD
    UI[UIモジュール: AvatarWindow/バルーン]
    Core[コアモジュール: CoreModule]
    Twitch[Twitchモジュール: TwitchReader]
    STT[STTモジュール: STTManager]
    AI[AIモジュール: AIClientManager]
    SM[AIモジュール内部: SearchManager]

    %% 要求フロー (スレッド呼び出し)
    UI -- 1. スレッド呼び出し --> Core
    Core -- 2. 要求 --> Twitch
    Core -- 2. 要求 --> STT
    Core -- 2. 要求 --> AI
    AI -. 2.5 検索実行 .-> SM

    %% イベント通知フロー (非同期通知)
    Twitch -- 3. on_notify_events --> Core
    STT -- 3. on_notify_events --> Core
    AI -- 3. on_notify_events --> Core
    Core -- 4. on_notify_events --> UI
```

### 1.1 モジュール責務一覧

| モジュール名 | 主要クラス名 | 動作スレッド | 主な責務 |
| :--- | :--- | :--- | :--- |
| **UIモジュール** | `AvatarWindow` | メイン（GUI）スレッド | ・アバターウィンドウの描画（750x480の左右2ペイン構成）<br>・直接テキスト入力欄（チャットタブ）の提供<br>・設定タブ（設定保存・適用、Twitch OAuth認可、WebHook設定）の提供<br>・OBS配信連携用WebSocketサーバー（ブロードキャスト）の提供<br>・最新AI応答表示領域（吹き出し風装飾されたQTextBrowser）の提供<br>・アバター状態変化やAI応答テキストの外部WebHookへのPOST送信（非同期）<br>・ユーザー操作の受付とコアへの要求発行、イベント受信による表示更新 |
| **Twitchモジュール**| `TwitchReader` | Twitchスレッド | ・認証トークンがない場合等にブラウザでOAuth画面（`force_verify=true`）を開き、一時HTTPサーバーを構築してリダイレクトを受け、JavaScript付きHTMLを返してURLハッシュ（フラグメント）からアクセストークンを受信・保存する<br>・取得したトークンを用いたTwitchチャット接続（WebSocket）<br>・コメント監視およびウェイクワード判定<br>・マッチしたコメントのイベント通知 |
| **コアモジュール** | `CoreModule` | コアスレッド | ・システム全体の制御および他モジュールの管理<br>・UIからの要求のハンドリング<br>・各モジュールからのイベント受信と処理フローの進行<br>・UIへの完了イベント通知 |
| **STTモジュール** | `STTManager` | STTスレッド | ・マイクからの音声キャプチャ（QAudioSource等を使用）<br>・`whisper.cpp` または `Windows SAPI` による音声認識<br>・文字起こし結果のイベント通知 |
| **AIモジュール** | `AIClientManager`<br>`IAIClient`<br>`SearchManager` | AIスレッド | ・**【2段構成＆検索連携】**<br>・**1段目（Manager）**: コアからの要求受付、AIクライアントの動的切り替え、共通イベント化と通知。および翻訳コマンド (`trans`) の検出と履歴・コンテキストのバイパス制御<br>・**2段目（Client）**: 各AI API固有のHTTPリクエスト構築とレスポンスパース、Function Calling (web_search) 時の再問い合わせ制御<br>・**検索マネージャ**: Tavily/DuckDuckGoを組み合わせたハイブリッドWeb検索および自動フォールバックの実行 |

---

## 2. スレッドモデル
Qtの `QThread` および `QObject::moveToThread` を活用したワーカーオブジェクトモデルを採用する。

1. **常駐スレッドのライフサイクル**
   - アプリケーション起動時（`main.cpp` または `AvatarWindow` の初期化時）に、各スレッド（`CoreThread`, `TwitchThread`, `STTThread`, `AIThread`）を生成・開始（`QThread::start()`）する。
   - 各ワーカーオブジェクト（`CoreModule`, `TwitchReader` 等）を生成し、対応するスレッドに `moveToThread` する。
2. **安全な終了処理（クリーンアップ）**
   - アプリケーション終了時（`closeEvent` の検知時）、UIからコアモジュールへ終了要求を送る。
   - コアモジュールは各モジュールに停止シグナルを送り、ループや接続を安全に終了させる。
   - その後、各 `QThread` に対して `quit()` を呼び出し、`wait()` でスレッドの完全終了（JOIN）を待機してからオブジェクトを破棄する。

---

## 3. イベント駆動モデル (データ連携)
モジュール間の連携には、Qtの **シグナルとスロット (QueuedConnection)** を利用した非同期イベント通信を用いる。

### 3.1 共通イベント構造体 `AppEvent`
モジュール間でやり取りするデータは、以下の構造体に統合して伝達する。

```cpp
enum class EventType {
    TwitchCommentReceived,  // トリガーとなるTwitchチャットを受信した
    VoiceInputStarted,       // 音声入力が開始された
    VoiceInputCompleted,     // 音声認識が完了しテキストが得られた
    AIRequestSent,          // AIへのAPIリクエストを送信した
    AIResponseReceived,     // AIからの応答テキストを受信した
    ErrorOccurred           // エラーが発生した
};

struct AppEvent {
    EventType type;
    QString text;           // 受信したチャット文、音声認識結果、AIの返答など
    QString source;         // イベント送信元モジュール名
    QVariantMap extraData;  // その他のメタデータ（ユーザー名、エラーコード等）
};
```

### 3.2 イベントの流れとシグナル・スロット接続

1. **個別モジュールからコアモジュールへの通知**
   - 各個別モジュールは `notifyEvent(const AppEvent& event)` シグナルを持つ。
   - コアモジュールはスロット `void on_notify_events(const AppEvent& event)` を持ち、各モジュールのシグナルと接続する。
2. **コアモジュールからUIモジュールへの通知**
   - コアモジュールは `notifyEventToUI(const AppEvent& event)` シグナルを持つ。
   - UIモジュールはスロット `void on_notify_events(const AppEvent& event)` を持ち、コアモジュールのシグナルと接続する。

---

## 4. クラス設計と拡張性

### 4.1 音声入力のエンジン抽象化 (`ISTTEngine`)
`whisper.cpp` と `Windows SAPI` を設定によって柔軟に切り替えるため、インターフェースクラスを導入する。

```mermaid
classDiagram
    class ISTTEngine {
        <<interface>>
        +initialize() bool
        +startListening() void
        +stopListening() void
        +setLanguage(QString lang) void
    }
    class WhisperEngine {
        -whisper_context* ctx
        +initialize() bool
        +startListening() void
    }
    class SAPIEngine {
        -ISpRecognizer* recognizer
        +initialize() bool
        +startListening() void
    }
    ISTTEngine <|.. WhisperEngine
    ISTTEngine <|.. SAPIEngine
    STTManager --> ISTTEngine : uses
```

### 4.2 AIモジュールの2段構成設計 (`AIClientManager` と `IAIClient`)
コアモジュールからの呼び出しインタフェースを一本化しつつ、接続先AI（Mistral AI、将来的な他のAI、テスト用ダミー等）の切り替えや各API固有の処理を隠蔽するため、**2段構成**を採用する。

```mermaid
classDiagram
    class CoreModule {
        +requestAI(QString text) void
    }
    class AIClientManager {
        -IAIClient* currentClient
        -QList<QPair<QString, QString>> m_chatHistory
        +on_requestAI(QString text) void
        +setAIProvider(QString provider) void
        +getChatHistory() QList<QPair<QString, QString>>
        +resetSession(bool isManual) void
        +importSessionBackup(QString filePath) bool
        +exportSessionBackup(QString encPath, QString txtPath) void
        <<signal>>
        +notifyEvent(AppEvent event)
        +chatHistoryUpdated(QList<QPair<QString, QString>> history)
    }
    class IAIClient {
        <<interface>>
        +sendRequest(QString prompt, const QList<QPair<QString, QString>>& history, const QString& sessionContext) void
        +setApiKey(QString apiKey) void
        <<signal>>
        +requestFinished(QString responseText, bool success)
    }
    class MistralAIClient {
        -QNetworkAccessManager* networkManager
        -SearchManager* m_searchManager
        +sendRequest(QString prompt, const QList<QPair<QString, QString>>& history, const QString& sessionContext) void
    }
    class DummyAIClient {
        +sendRequest(QString prompt, const QList<QPair<QString, QString>>& history, const QString& sessionContext) void
    }
    class SearchManager {
        -ISearchProvider* m_currentProvider
        -QString m_tavilyApiKey
        +executeSearch(const QString& query) void
        <<signal>>
        +searchFinished(const QString& resultText, bool success)
    }
    class ISearchProvider {
        <<interface>>
        +search(const QString& query) void
        <<signal>>
        +searchFinished(const QString& resultText, bool success)
    }
    class TavilySearchProvider {
        -QNetworkAccessManager* m_networkManager
        -QString m_apiKey
        +search(const QString& query) void
    }
    class DuckDuckGoSearchProvider {
        -QNetworkAccessManager* m_networkManager
        +search(const QString& query) void
    }

    CoreModule --> AIClientManager : 1.要求 (シグナル/スロット)
    AIClientManager --> IAIClient : 2.処理の委譲 (抽象インターフェース)
    IAIClient <|.. MistralAIClient : 3.具象実装
    IAIClient <|.. DummyAIClient : 3.具象実装
    MistralAIClient --> SearchManager : 4.検索要求
    SearchManager --> ISearchProvider : 5.委譲
    ISearchProvider <|.. TavilySearchProvider
    ISearchProvider <|.. DuckDuckGoSearchProvider
```

#### 2段構成の責務分担

1. **1段目：`AIClientManager`（スレッド窓口・共通管理）**
   - コアスレッドからの要求をスロット（`on_requestAI`）で受信。コアモジュールからは常にこのクラスのみが見える。
   - 現在の設定に基づき、適切な `IAIClient` 具象クラス（2段目）へ処理を委譲する。
   - 2段目から返ってきた処理結果（テキストや成否フラグ）を受け取り、共通の `AppEvent` 構造体に組み立ててコアモジュールへ通知する。
   - APIキーの管理や、タイムアウト制御などの各AIで共通する処理をここで吸収する。

2. **2段目：`IAIClient` 具象クラス（各AI API固有の個別処理）**
   - HTTPのヘッダーおよびペイロード（JSON）の構築。
   - `QNetworkAccessManager` を使用した各API固有のエンドポイントへの通信。
   - 各API固有のJSONレスポンスのパースと、最終的な返答テキストの抽出。

### 4.3 アバター画像と表示状態の管理設計
アバター画像（静止画）の切り替えによる簡易アニメーションを制御するため、アバターの「表示状態」を定義し、UIモジュール側で状態に応じた画像を管理する。

#### A. アバター表示状態の定義
アバターはシステムの状態（イベント）と連動し、以下の4つの状態を遷移する。

| 状態名 (`AvatarState`) | 説明 | 連動するイベントの例 | 表示画像の例 |
| :--- | :--- | :--- | :--- |
| `Idle` | 通常の待機状態 | アプリ起動時、バルーン非表示時 | `pic/idle.png` |
| `Listening` | 音声認識の待ち受け状態 | `VoiceInputStarted` 受信時 | `pic/listening.png` |
| `Thinking` | AI応答の待機中（思考中） | `AIRequestSent` 受信時 | `pic/thinking.png` |
| `Speaking` | バルーンにAIの応答を表示中 | `AIResponseReceived` 受信時 | `pic/speaking.png` |

#### B. 画像ファイルの管理方式
アバターの状態に応じた画像や透過用パラメータ、および描画基準点を外部のJSON設定ファイル（例: `pic/avatar_settings.json`）で定義します。

**JSON設定ファイル構造例:**
```json
{
  "idle": {
    "file": "idle.png",
    "anchorX": 120,
    "anchorY": 180,
    "transparentX": 0,
    "transparentY": 0
  },
  "listening": {
    "file": "listening.png",
    "anchorX": 120,
    "anchorY": 180,
    "transparentX": 0,
    "transparentY": 0
  },
  "thinking": {
    "file": "thinking.png",
    "anchorX": 120,
    "anchorY": 182,
    "transparentX": 0,
    "transparentY": 0
  },
  "speaking": {
    "file": "speaking.png",
    "anchorX": 124,
    "anchorY": 180,
    "transparentX": 0,
    "transparentY": 0
  }
}
```

* **【自動背景透過処理 (Flood Fill または カラーキー透過)】**
  * 各画像のロード時に、設定された座標（例: `transparentX`, `transparentY`。未指定の場合は左上 `(0,0)`）のピクセル色に基づき、外側の連結成分のみを透明にする **Flood Fill 方式** または全体透過処理を行います。
  * **画像ロード時（メモリ展開時）にこの透過処理を一括して完了させ、透過済みの `QPixmap` としてキャッシュに保持します。**
  * アニメーション（画像切り替え）の瞬間には、すでに透過処理が完了している `QPixmap` を描画領域にセットするだけであるため、切り替え時に背景のクロマキー色（緑やピンクなど）が一瞬映り込んだり、描画の遅延で**ちらついたりする現象（フリッカー）は発生しません。**

* **【アンカーポイントによる位置ズレ補正描画】**
  * 画像ごとにキャラクターの「足元」や「重心」などの基準座標をアンカー（`anchorX`, `anchorY`）として定義します。
  * 状態変化に伴い画像のサイズが異なる場合でも、デスクトップ上のマスコットの目標位置 `(targetX, targetY)` にアンカーが重なるように、ウィンドウ（または画像描画ラベル）の座標を `(targetX - anchorX, targetY - anchorY)` に動的に補正・移動させます。これにより、切り替え時の不自然なキャラクターの位置ズレやガタつきを防止します。

- UIモジュール（`AvatarWindow`）が透過処理済みの `QPixmap` とアンカー設定をキャッシュし、コアモジュールからのイベント（`AppEvent`）検知時に表示画像と描画位置を補正して切り替えます。

---

## 5. 主要シーケンス

### 5.1 音声入力またはTwitchコメントからの処理フロー

```mermaid
sequenceDiagram
    autonumber
    actor User as ユーザー / 視聴者
    participant UI as UIモジュール (AvatarWindow)
    participant Core as コアモジュール (CoreModule)
    participant STT as STTモジュール (STTManager)
    participant AI as AIモジュール (AIClientManager)

    %% 音声入力の例
    User->>UI: マイクに向かって話す (音声入力トリガー押下)
    UI->>Core: スレッド経由で処理要求 (startVoiceInput)
    Core->>STT: 音声キャプチャ & 認識開始要求

    STT->>STT: 音声バッファを取得 & SAPI/Whisperでテキスト化
    STT->>Core: notifyEvent (VoiceInputCompleted, text)
    
    %% コアがAI要求を発行
    Core->>Core: on_notify_eventsで受信・文言チェック
    Core->>UI: notifyEventToUI (アバターを考え中アニメに変更)
    Core->>AI: AIリクエスト要求 (text)

    AI->>AI: APIリクエスト送信 (Mistral API)
    AI->>Core: notifyEvent (AIResponseReceived, ai_text)

    %% コアがUIへ通知
    Core->>Core: on_notify_eventsで受信・履歴保存等
    Core->>UI: notifyEventToUI (AIResponseReceived, ai_text)
    UI->>UI: on_notify_eventsで受信
    UI->>UI: 右ペインのテキストエリアにai_textをマークダウン表示、アバターを通常アニメに変更
```

### 5.2 テキスト直接入力からの処理フロー

```mermaid
sequenceDiagram
    autonumber
    actor User as ユーザー
    participant UI as UIモジュール (AvatarWindow)
    participant Core as コアモジュール (CoreModule)
    participant AI as AIモジュール (AIClientManager)

    User->>UI: キーボードから命令を入力 & 送信 (Enter等)
    UI->>Core: スレッド経由で処理要求 (submitDirectInput, text)
    
    %% コアが直接AI要求を発行 (STTをバイパス)
    Core->>Core: on_notify_eventsで受信・文言チェック
    Core->>UI: notifyEventToUI (アバターを考え中アニメに変更)
    Core->>AI: AIリクエスト要求 (text)

    AI->>AI: APIリクエスト送信 (Mistral API)
    AI->>Core: notifyEvent (AIResponseReceived, ai_text)

    %% コアがUIへ通知
    Core->>Core: on_notify_eventsで受信・履歴保存等
    Core->>UI: notifyEventToUI (AIResponseReceived, ai_text)
    UI->>UI: on_notify_eventsで受信
    UI->>UI: 右ペインのテキストエリアにai_textをマークダウン表示、アバターを通常アニメに変更
```

### 5.3 起動時出自証明およびコピーライト動的適用シーケンス

```mermaid
sequenceDiagram
    autonumber
    participant Main as メインエントリー (main.cpp)
    participant TC as TrustChain (Core)
    participant UI as UIモジュール (AvatarWindow)
    participant Helper as TrustChain (QtHelper)

    Main->>TC: verifyToken() の実行
    alt オンライン検証成功（公式ビルド）
        TC-->>Main: AuthStatus::Normal を返却
    else 検証失敗（非公式・オフライン・エラー）
        TC-->>Main: AuthStatus::Watermarked を返却
    else トークン無効化（ブラックリスト）
        TC-->>Main: AuthStatus::Terminated を返却
        Main->>TC: terminateApplication()
        Note over Main,TC: アプリケーション強制シャットダウン (qFatal)
    end

    Main->>UI: AvatarWindow のインスタンス生成
    Main->>Helper: applyWatermark(window, status)
    
    alt status == AuthStatus::Watermarked
        Helper->>Helper: 自身 (.exe) から BinMarkManager 署名スキャン
        alt 署名が存在する (Plain=...)
            Helper-->>UI: タイトルバー・ステータスバーに抽出したコピーライトを設定
        else 署名が存在しない
            Helper-->>UI: タイトルバー・ステータスバーにフォールバックコピーライトを設定
        end
    end
    Main->>UI: window.show() (通常どおり起動)
```

### 5.4 セッションリセットシーケンス

```mermaid
sequenceDiagram
    autonumber
    actor User as ユーザー (手動) / システム (自動)
    participant UI as UIモジュール (AvatarWindow)
    participant Core as コアモジュール (CoreModule)
    participant AI as AIモジュール (AIClientManager)
    participant TC as 暗号化モジュール (TransCipher)

    alt 手動リセットの場合
        User->>UI: 右クリックメニューから「会話履歴をリセット」を選択
        UI->>Core: resetSessionRequested() シグナル発火
        Core->>AI: resetSession(true) 呼び出し
    else 自動リセットの場合 (会話量が制限値に到達)
        AI->>AI: 対話完了時、履歴数が規定値 (例: 10件) に到達したことを検知
        AI->>AI: resetSession(false) を自動でトリガー
    end

    %% AIによるマークダウン要約の生成
    AI->>AI: 履歴 m_chatHistory を元に「コンテキスト要約要求」をAI APIへ送信
    Note over AI: 要約プロンプト: 「これまでの対話をマークダウンでまとめてください」
    AI-->>AI: マークダウン要約テキストを受信
    AI->>AI: 要約テキストを平文ファイル log/session_context.md に保存

    %% バックアップとクリア処理
    AI->>TC: 現在の m_chatHistory を JSON化し TransCipher で暗号化
    TC-->>AI: 暗号化データをファイル log/session_backup_<timestamp>.enc に保存
    AI->>AI: メモリ上の m_chatHistory をクリア

    %% イベント通知
    alt 手動リセットの場合のみ
        AI->>Core: notifyEvent (EventType::AIResponseReceived, text: "会話履歴をクリアし、コンテキスト要約を保存しました。")
        Core->>UI: notifyEventToUI (...)
        UI->>UI: バルーンに「会話履歴をクリアし、コンテキスト要約を保存しました。」を表示
    else 自動リセットの場合
        Note over AI,UI: UI通知は行わず、サイレントに新セッションを開始
    end
```

### 5.5 セッションインポートシーケンス

```mermaid
sequenceDiagram
    autonumber
    actor User as ユーザー
    participant UI as UIモジュール (AvatarWindow)
    participant Core as コアモジュール (CoreModule)
    participant AI as AIモジュール (AIClientManager)
    participant TC as 暗号化モジュール (TransCipher)

    User->>UI: 右クリックメニューから「会話履歴をインポート...」を選択
    UI->>UI: QFileDialog で .enc バックアップファイルを選択
    UI->>Core: importSessionRequested(filePath) シグナル発火
    Core->>AI: requestSessionImport(filePath) を中継
    AI->>TC: TransCipher::decrypt() で指定ファイルを復号
    TC-->>AI: 復号された JSON データを返却
    alt 復号 & パース成功
        AI->>AI: m_chatHistory を復号された会話履歴で上書き
        AI->>AI: chatHistoryUpdated() シグナルで履歴同期
        AI->>Core: notifyEvent (AIResponseReceived, "会話履歴をインポートしました。")
    else 失敗（破損、鍵不一致等）
        AI->>Core: notifyEvent (ErrorOccurred, "会話履歴のインポートに失敗しました。")
    end
    Core->>UI: notifyEventToUI (...)
    UI->>UI: バルーンに結果メッセージを表示
```

### 5.6 セッションエクスポート（復号）シーケンス

```mermaid
sequenceDiagram
    autonumber
    actor User as ユーザー
    participant UI as UIモジュール (AvatarWindow)
    participant Core as コアモジュール (CoreModule)
    participant AI as AIモジュール (AIClientManager)
    participant TC as 暗号化モジュール (TransCipher)

    User->>UI: 右クリックメニューから「会話履歴をエクスポート...」を選択
    UI->>UI: QFileDialog で復号対象の .enc ファイルを選択
    UI->>UI: QFileDialog でエクスポート先 .txt パスを指定
    UI->>Core: exportSessionRequested(encPath, txtPath) シグナル発火
    Core->>AI: requestSessionExport(encPath, txtPath) を中継
    AI->>TC: TransCipher::decrypt() で .enc ファイルを復号
    TC-->>AI: 復号された JSON データを返却
    alt 復号成功
        AI->>AI: 人間が読みやすい平文テキスト形式にフォーマット
        AI->>AI: 指定された .txt パスへ書き出し
        AI->>Core: notifyEvent (AIResponseReceived, "会話履歴をエクスポートしました。")
    else 失敗
        AI->>Core: notifyEvent (ErrorOccurred, "会話履歴のエクスポートに失敗しました。")
    end
    Core->>UI: notifyEventToUI (...)
    UI->>UI: 右ペインに結果メッセージを表示
```

### 5.7 設定更新およびTwitch再認可シーケンス (Implicit Flow)

```mermaid
sequenceDiagram
    autonumber
    actor User as ユーザー
    participant UI as UIモジュール (AvatarWindow)
    participant Core as コアモジュール (CoreModule)
    participant AI as AIモジュール (AIClientManager)
    participant Twitch as Twitchモジュール (TwitchReader)
    participant Browser as Webブラウザ
    participant TwitchAPI as Twitch API (id.twitch.tv)

    %% 設定更新
    User->>UI: 設定タブから設定を変更して「保存して適用」を押下
    UI->>UI: local_settings.json に設定を上書き保存（twitch_client_secret, twitch_refresh_tokenは不要のため削除）
    UI->>Core: settingsUpdated() シグナル発火
    Core->>AI: settingsUpdated() 中継
    Core->>Twitch: settingsUpdated() 中継
    AI->>AI: local_settings.json を再ロードしてクライアント再初期化
    Twitch->>Twitch: local_settings.json を再ロードして監視設定を更新

    %% Twitch再認可 (Implicit Flow)
    User->>UI: 設定タブから「Twitch認証開始」を押下
    UI->>Core: twitchReauthRequested() シグナル発火
    Core->>Twitch: requestTwitchReauth() 中継
    Twitch->>Twitch: 現在のWebSocket接続を切断し、OAuthトークンをクリア
    Twitch->>Twitch: 一時HTTPサーバーを起動 (リダイレクト受付用)
    Twitch->>Browser: 認可画面URLを開く (response_type=token, force_verify=true)
    Browser->>TwitchAPI: アカウントでログイン & 認可
    TwitchAPI-->>Browser: フラグメント付きURLへリダイレクト (http://localhost:port/#access_token=...)
    Browser->>Twitch: 一時HTTPサーバーにアクセス (ハッシュを含むリダイレクト先)
    Twitch-->>Browser: JavaScript付きHTMLを返却 (ハッシュ値をクエリパラメータに変換して再送信するスクリプト)
    Browser->>Twitch: /token エンドポイントへ再リクエスト (GET /token?access_token=...)
    Twitch->>Twitch: access_token を受信・抽出
    Twitch->>Twitch: access_token を local_settings.json に保存
    Twitch->>Twitch: 一時HTTPサーバーを停止し、Twitchチャットへの接続開始
```

### 5.8 Twitch接続エラー時の手動再認可要求シーケンス

```mermaid
sequenceDiagram
    autonumber
    participant Twitch as Twitchモジュール (TwitchReader)
    participant Core as コアモジュール (CoreModule)
    participant UI as UIモジュール (AvatarWindow)

    Note over Twitch: チャット接続中の認証エラー（IRC認証失敗など）を検知
    Twitch->>Core: notifyEvent (ErrorOccurred, "Twitch認証の有効期限が切れました。再認可を行ってください。")
    Core->>UI: notifyEventToUI (...)
    UI->>UI: バルーンやステータスバーでユーザーに再認可を促すメッセージを表示
```

### 5.9 OBS配信連携用WebSocket・WebHook配信シーケンス

```mermaid
sequenceDiagram
    autonumber
    actor OBS as OBS Studio (ブラウザソース)
    participant UI as UIモジュール (AvatarWindow)
    participant Core as コアモジュール (CoreModule)
    actor WH as WebHook受信先 (外部サーバー/ツール)

    OBS->>UI: avatar_obs.html ロード時に WebSocket 接続 (ws://localhost:58081)
    UI-->>OBS: 初期状態の同期 (Init イベントデータ)

    alt アバター状態変更時
        UI->>UI: updateAvatarDisplay() 実行
        UI->>OBS: QWebSocketServer経由でブロードキャスト (AvatarChanged JSON)
        opt WebHook有効時
            UI->>WH: POST (Webhook URL) へ非同期送信 (AvatarChanged JSON)
        end
    else AI応答受信時
        Core->>UI: notifyEventToUI (AIResponseReceived, text)
        UI->>UI: 右ペインに表示設定
        UI->>OBS: QWebSocketServer経由でブロードキャスト (AIResponseReceived JSON)
        opt WebHook有効時
            UI->>WH: POST (Webhook URL) へ非同期送信 (AIResponseReceived JSON)
        end
    end
```

### 5.9 ハイブリッドWeb検索（Function Calling）シーケンス

```mermaid
sequenceDiagram
    autonumber
    participant UI as UIモジュール (AvatarWindow)
    participant Core as コアモジュール (CoreModule)
    participant AI as AIモジュール (AIClientManager)
    participant Client as MistralAIClient
    participant SM as SearchManager
    participant Provider as ISearchProvider (Tavily/DDG)
    participant Web as 外部API/Webサーバー

    UI->>Core: ユーザーからの質問送信
    Core->>AI: AIリクエスト要求 (prompt)
    AI->>Client: sendRequest(prompt, history)
    Client->>Web: Mistral API へリクエスト送信 (tools定義を含む)
    Web-->>Client: tool_calls (web_search, query) を返却
    Client->>SM: executeSearch(query)
    SM->>Provider: search(query) (設定に応じたプロバイダ)
    Provider->>Web: 検索リクエスト送信
    Web-->>Provider: 検索結果データ
    Provider-->>SM: 検索結果テキスト
    SM-->>Client: searchFinished(resultText, success)
    Client->>Web: Mistral API へ再リクエスト送信 (tool_callsの内容 + 検索結果)
    Web-->>Client: 最終回答テキスト (choices.message.content)
    Client->>AI: requestFinished(responseText, true)
    AI->>Core: notifyEvent (AIResponseReceived, responseText)
    Core->>UI: notifyEventToUI (AIResponseReceived, responseText)
```

### 5.10 Tavilyエラー時の自動フォールバックシーケンス

```mermaid
sequenceDiagram
    autonumber
    participant Client as MistralAIClient
    participant SM as SearchManager
    participant Tavily as TavilySearchProvider
    participant DDG as DuckDuckGoSearchProvider
    participant Web as 外部API/Webサーバー

    Client->>SM: executeSearch(query)
    Note over SM: Tavily APIキーが設定されているため<br/>TavilySearchProviderを選択
    SM->>Tavily: search(query)
    Tavily->>Web: Tavily Search API (POST)
    alt APIキー無効 / 無料枠超過 (HTTP 403/429)
        Web-->>Tavily: エラーレスポンス (HTTP Error)
        Tavily-->>SM: searchFinished(errorText, false)
        Note over SM: エラーを検知し、自動的かつ<br/>サイレントにフォールバック処理を開始
        SM->>DDG: search(query)
        DDG->>Web: DuckDuckGo HTML 取得 (GET)
        Web-->>DDG: HTMLソース
        DDG->>DDG: 正規表現でスニペットをパース・整形
        DDG-->>SM: searchFinished(ddgText, true)
        SM-->>Client: searchFinished(ddgText, true)
    end
```

---




## 6. セキュリティとライセンス検証 (TrustChain & BinMarkManager)
本システムは、配布バイナリの出自を保証し改ざんを検知するため、`TrustChain` モジュールを組み込む。

1. **出自証明のライフサイクル**:
   - ビルド時に、開発環境の Git コミットハッシュと GitHub リモートマスタを照合し、事前登録したトークン (`tc_43f73638ed2a119f983e999b`) とともにコンパイルマクロとしてバイナリに安全に注入する。
   - 起動時に、注入された情報を用いて、オンライン検証サーバーへ認証を問い合わせる。
2. **改ざん検知時のUI表示**:
   - 改ざん（検証失敗、オフライン、非公式ビルド）検知時は、自身の実行バイナリ末尾から `BinMarkManager` 形式の平文コピーライトを動的抽出し、メインウィンドウ（`AvatarWindow`）のタイトルバーとステータスバーに強制的に表示する。
   - アプリケーションとしての機能制限は一切加えず、表示の強制変更のみに留める。

### 5.11 翻訳コマンド処理フロー

```mermaid
sequenceDiagram
    autonumber
    actor User as 視聴者 / ユーザー
    participant UI as UIモジュール (AvatarWindow)
    participant Core as コアモジュール (CoreModule)
    participant AI as AIモジュール (AIClientManager)
    participant Client as MistralAIClient

    User->>UI: コマンド送信 (例: !ai trans en こんにちは)
    UI->>Core: requestAIExecution ("trans en こんにちは")
    Core->>AI: requestAI ("trans en こんにちは")

    AI->>AI: 先頭の "trans" コマンドおよび引数を検出
    AI->>AI: ターゲット言語(English)と翻訳対象("こんにちは")をパース
    AI->>AI: m_isTranslationRequest = true を設定
    AI->>AI: 履歴・コンテキストを「空」に設定し、翻訳指示プロンプトを構築

    AI->>Client: sendRequest(translationPrompt, 空履歴, 空コンテキスト)
    Client->>Client: APIリクエスト送信 (翻訳結果のみ出力するように指示)
    Client-->>AI: 翻訳結果 ("Hello") を返却

    AI->>AI: 翻訳要求の完了を検知 (履歴追加・ログ保存をバイパス)
    AI->>AI: m_isTranslationRequest = false に戻す
    AI->>Core: notifyEvent (AIResponseReceived, "Hello")
    Core->>UI: notifyEventToUI (AIResponseReceived, "Hello")
    UI->>UI: 右ペインに "Hello" を表示
```

---

## 7. 今後の課題（詳細設計に向けた検討）
1. **ライブラリのビルド環境設定:**
   - Qt6 C++ および C++20 環境の構築。
   - `whisper.cpp` の C++ プロジェクトへの静的/動的リンク手法。
   - Windows SAPI (COMインターフェース) の利用設定。
   - Mistral API（HTTPS）を叩くための `QNetworkAccessManager` (Qt Networkモジュール) の追加。
2. **透過ウィンドウの実現性:**
   - Qtにおける透明度やクリック透過の設定（`Qt::WA_NoSystemBackground`, `Qt::WA_TranslucentBackground`）。
