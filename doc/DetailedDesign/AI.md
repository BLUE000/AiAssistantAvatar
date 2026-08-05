# 詳細設計書 - AI モジュール (DetailedDesign/AI.md)

## 1. 概要
本ドキュメントは、AI Assistant Avatar における AI クライアント群、プロバイダ自動選定・ルーティング (`AIRouter`)、レートリミット追跡 (`RateLimitTracker`)、段階的タスク分解パイプライン (`analyzeAndDecomposeTasks`)、および役割分離プロンプト構築 (`formatRoleSeparatedPrompt`) の詳細設計を定義する。

---

## 2. クラス構造とインターフェース

### 2.1 `IAIClient` インターフェース
全 AI プロバイダクライアントが継承する基底抽象クラス。
```cpp
class IAIClient : public QObject {
    Q_OBJECT
public:
    explicit IAIClient(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~IAIClient() = default;

    virtual QString providerName() const = 0;
    virtual QString apiKey() const = 0;
    virtual void sendRequest(const QString &prompt, 
                             const QList<QPair<QString, QString>> &chatHistory,
                             const QString &sessionContext = "",
                             const QString &additionalSystemPrompt = "") = 0;

signals:
    void responseReceived(const QString &response);
    void errorOccurred(const QString &errorMessage);
};
```

---

## 3. 実装クライアントクラス群

### 3.1 `SakuraAIClient` (さくらAI / OpenAI互換)
- エンドポイント: Custom / Sakura API URL
- 送信フォーマット: JSON (`model`, `messages`)

### 3.2 `GeminiAIClient` (Google Gemini API)
- エンドポイント: `https://generativelanguage.googleapis.com/v1beta/models/...`
- 特徴: `systemInstruction` パラメータへの `additionalSystemPrompt` 注入。

### 3.3 `GroqAIClient` (Groq API)
- エンドポイント: `https://api.groq.com/openai/v1/chat/completions`

### 3.4 `MistralAIClient` (Mistral AI API)
- エンドポイント: `https://api.mistral.ai/v1/chat/completions`

### 3.5 `CerebrasAIClient` (Cerebras API)
- エンドポイント: `https://api.cerebras.ai/v1/chat/completions`

### 3.6 `HuggingFaceAIClient` (HuggingFace Inference API)
- エンドポイント: `https://api-inference.huggingface.co/models/...`

### 3.7 `OpenRouterAIClient` (OpenRouter API)
- エンドポイント: `https://openrouter.ai/api/v1/chat/completions`

### 3.8 `DummyAIClient` (開発・テスト用ダミーAI)
- Debug ビルド (`#ifdef QT_DEBUG`) でのみ稼働。Release ビルドでは自動選定対象から除外。

---

## 4. `AIClientManager` ＆ `RateLimitTracker` ルーティング設計

### 4.1 使用枠最大 AI 自動選定ロジック (`RateLimitTracker::selectBestAvailableClient`)
```cpp
QString RateLimitTracker::selectBestAvailableClient() {
    // 全設定済みプロバイダの中から、現在レートリミット到達でなく、
    // 残り利用可能枠 (RPM/RPD) が最も大きいプロバイダを評価・選定
}
```

### 4.2 APIキー未設定 ＆ レートリミット全枯渇ガード (`F-16-8`)
1. **全 API キー未設定時**:
   - 外部リクエストを遮断し、「*APIキーが設定されていません。設定画面から入力してください*」と即時通知。
2. **全プロバイダ レートリミット到達時 (二分岐案内)**:
   - 最長リセット時間を算出し、未設定キーの有無に応じて案内メッセージを自動生成：
     - *未設定キーあり*: 「*全プロバイダが上限到達中。解除まで【約〇分〇秒】。未設定の[〇〇]キーを登録するとすぐ使えます*」
     - *全キー設定済み*: 「*すべてのAIプロバイダが上限到達中。解除まで【約〇分〇秒】ほどお待ちください*」

### 4.3 3段階ハイブリッド レートリミット追跡 ＆ 自律学習アルゴリズム
1. **暫定上限値の初期化 (Web / Config Fallback)**:
   - レスポンスヘッダーに制限情報が含まれないプロバイダ（Sakura, HuggingFace, OpenRouter等）は、Web公開仕様に基づく設定値 (`provider_limits`) を $RPM_{max}, RPD_{max}, TPM_{max}, TPD_{max}$ の初期暫定値として適用する。
2. **ローカル内部消費計算モデル (Local Consumption Estimator)**:
   - APIヘッダーが取得できない環境下において、1リクエスト完了ごとにローカル内部で残量をカウントダウン減算する：
     $$RPM_{rem} \leftarrow \max(0, RPM_{rem} - 1)$$
     $$RPD_{rem} \leftarrow \max(0, RPD_{rem} - 1)$$
   - 送信プロンプト長 $L_{in}$ および応答文字列長 $L_{out}$ から概算トークン消費量 $T_{est}$ を算出：
     $$T_{est} = \lceil (L_{in} \cdot c_{in} + L_{out} \cdot c_{out}) \cdot \alpha \rceil$$
     （初期安全マージン係数 $\alpha = 1.2$, 日本語文字換算比率 $c_{in} = c_{out} = 1.3$）
     $$TPM_{rem} \leftarrow \max(0, TPM_{rem} - T_{est})$$
     $$TPD_{rem} \leftarrow \max(0, TPD_{rem} - T_{est})$$
3. **適応学習型自己校正 (Adaptive Calibration Engine)**:
   - **429（制限超過）検知時の学習**: 429発生時の推定残量を実質境界ラベルとみなし、安全マージン係数を $\alpha \leftarrow \min(3.0, \alpha \times 1.25)$ へ自動引き上げ。次回のリセット時間予測を自律更新。
   - **ヘッダー実測同期時の学習**: 正確なレスポンスヘッダーが得られたタイミングで残量を実測同期し、推定誤差率に基づき EMA（指数移動平均）で安全係数 $\alpha$ を適応補正する。
   - **使えば使うほど精度向上**: 利用履歴およびエラーフィードバックの蓄積に伴い、プロバイダごとの固有消費率および復帰タイマー精度が自律的に向上する。

---


## 5. 段階的タスク分解パイプライン ＆ 役割分離プロンプト構築 (`F-16-9`)

### 5.1 Intent判定最適化 (`analyzeAndDecomposeTasks`)
- 日時単語「今日」「明日」などの単体による Web 検索自動発火を完全廃止。
- 「予定」「スケジュール」「タスク」「カレンダー」が含まれる場合は Web 検索を自動スキップし、TaskFlow 優先ルーティングを確定。
- 「天気」「ニュース」「株価」「為替」「潮汐」などの明確な情報目的名詞が含まれる場合のみ Web 検索を発火。

### 5.2 プロンプト役割分離構築 (`formatRoleSeparatedPrompt`)
- `User` メッセージ（`finalPrompt`）には「今日の予定は？」という純粋なユーザー発言本文のみを格納。
- 事前収集データ（TaskFlow、Web検索結果、ナレッジ）はすべて `System` 指示領域（`additionalSystemPrompt` / `systemInstruction`）へ `[事前収集リファレンスデータ (現在日時: YYYY-MM-DD時点)]` として分離注入。

---

## 6. 稼働中 AI プロバイダ・適用モデル情報の自己回答システムプロンプト自動注入

### 6.1 コンテキスト注入フロー (`AIClientManager` ➜ `PromptBuilder`)
1. **稼働モデルの動的特定**:
   - リクエスト発生時、選定された Worker AI（例: Groq）および Manager AI（例: Groq）の「プロバイダ名」および「実際のリクエストで使用されているモデル名（自動選択で決定された実効モデル名を含む）」を取得。
2. **システムプロンプトへの自動動的挿入**:
   - `PromptBuilder` 内で以下のコンテキスト文字列を動的に挿入して AI クライアントへ送信する：

```markdown
【現在のAIシステム稼働ステータス】
- 会話用 Worker AI: %1 (適用モデル: %2)
- 評価・判断用 Manager AI: %3 (適用モデル: %4)
※ ユーザーから現在使用されている AI プロバイダやモデル名について尋ねられた場合は、上記の名称を正確に回答してください。
```

3. **応答効果**:
   - 「今使ってるAIモデルは何？」「マネージャーAIは何か動いてる？」等のチャットでの質問に対し、AI アバターが自身の最新稼働モデル名を 100% 正確に認識して即答可能となる。

---

## 6. チャット応答テキストの 500 文字自動分割 ＆ スローモード遅延キュー詳細設計

### 6.1 `splitTextForComment` 文字列分割アルゴリズム
- **メソッドシグネチャ**: `static QStringList splitTextForComment(const QString &text, int maxLen = 500)`
- **処理ロジック**:
  1. `text.length() <= maxLen` の場合、元のテキスト 1 件を含む `QStringList` をそのまま返す。
  2. `text.length() > maxLen` の場合、先頭から `maxLen` 文字（500文字）までの範囲内で、後ろから逆方向に最優先区切り文字（`。`, `！`, `？`, `\n`）を探索する。
  3. 優先区切り文字が見つかった場合はその位置で分割し、見つからない場合はカンマ（`、`, `,`）やスペースを探索し、それでも見つからない場合は `maxLen` 文字目で強制切断する。
  4. 残りの文字列に対して再帰的またはループで同様の分割を行い、500文字以内の chunk リスト（`QStringList`）を生成する。

### 6.2 スローモード対応遅延送信キュー (`CommentQueueManager`)
- **役割**: Twitch/Discord への送信要求を `QQueue<QString>` で保持し、スローモード対応の送出インターバル（タイマー制御、例: 1500ms）ごとに 1 メッセージずつ取り出して送出する。
- **データ構造・メンバー**:
  - `QQueue<QString> m_sendQueue;`
  - `QTimer *m_dispatchTimer;`
  - `int m_slowModeIntervalMs = 1500; // 初期値 1.5秒`
- **動作フロー**:
  1. AI 応答受信時、`splitTextForComment(response)` により生成されたリストを `m_sendQueue` に順次エンキューする。
  2. `m_dispatchTimer` が未稼働の場合、即座にタイマーを開始する。
  3. タイマータイムアウト（1500ms）毎に `m_sendQueue.dequeue()` を取り出し、`requestTwitchSend` / `requestDiscordSend` を発火して送出する。
  4. キューが空になった時点でタイマーを停止する。

---

## 7. レートリミット自動枠復帰 ＆ ルーター側イベント駆動ステータス更新詳細設計

### 7.1 `RateLimitTracker::updateAvailable` のルーター自動判定・復帰ロジック
- **タイマーフリー・イベント駆動設計**:
  - UI 側のタイマーは完全削除（タイマー詰まり・異常終了リスクを 0% 化）。
  - `AIClientManager`（ルーター/マネージャー）が `isAvailable` / `selectBestAvailableClient` / `statusOf` / `allStatuses` 等で状態を参照・更新するたびに `updateAvailable` が呼び出される。
- **評価手順**:
  1. `QDateTime nowUtc = QDateTime::currentDateTimeUtc();`
  2. 各プロバイダの `nextResetAt.isValid() && nextResetAt <= nowUtc` を判定。
  3. リセット時間を超過している場合:
     - `rpmRemaining = rpmMax;` （使用枠の自動再補充）
     - `nextResetAt = QDateTime();` （リセット状態完了）
  4. `rpmRemaining > 0` かつ `rpdRemaining > 0` を確認し、`available = true` （利用可能）へと自動回復させる。

### 7.2 UI（`RateLimitTabWidget`）の表示トリガー要求 ＆ 完全シグナル同期描画
- **UI 表示時トリガー**:
  - `RateLimitTabWidget` の `showEvent` （タブ表示時）または `QTabWidget` のタブ選択時に、`emit requestRefreshStatus();` シグナルを発火する。
  - `AIClientManager` は本要求を受信すると、最新の UTC 時間 `currentDateTimeUtc()` で全プロバイダの期限評価・復帰処理（`updateAvailable`）を即座に実行し、`notifyStatusUpdated(statuses)` シグナルを送返す。
- **完全タイマーフリー描画**:
  - UI 側にはタイマーを一切置かず、マネージャーからの `notifyStatusUpdated(statuses)` シグナルを受信したタイミングのみで描画を同期更新する。
  - 描画時、`status.nextResetAt` （UTC）と `QDateTime::currentDateTimeUtc()` の差分を計算し、`waitSec <= 0` かつ `status.available == true` の場合は `🟢 利用可能` （緑色 `#2e7d32`）をピタッと即時表示する。

---

## 8. レートリミット更新・表示コマンド応答およびシステムプロンプトナレッジ詳細設計

### 8.1 `SystemResponseManager::processPrompt` でのインターセプト処理
- **判定キー**: プロンプトに `レートリミット`, `リミット`, `制限` のキーワードが含まれ、かつ `更新`, `表示`, `教えて`, `見せて`, `/ratelimit`, `/status` 等の指示ワードが含まれる場合。
- **動作**:
  1. システムメッセージ `レートリミット情報を更新しました。「レートリミット」タブから各AIの利用枠や残量、解除カウントダウンをご確認いただけます。` を返却する。
  2. マネージャーに対し、`emitCurrentStatus()` （ステータス即時更新）を呼び出させて画面の表示を最新化する。

### 8.2 システムプロンプトへのナレッジ注入 (`AIClientManager::formatRoleSeparatedPrompt`)
- **注入内容**:
  `system` ロールプロンプトへ以下を常時追加注入する：
  > `[System Capability]`
  > `本アプリには、各AIプロバイダ（Groq, Mistral, Cerebras, Sakura AI, HuggingFace, OpenRouter等）のレートリミット使用枠（RPM/RPD）や残量、リセット時間をリアルタイム監視・更新・表示する『レートリミット』タブ機能が実装されています。ユーザーからレートリミットの表示や更新について尋ねられた場合は、アプリの『レートリミット』タブからいつでも確認・更新できる旨を正しく回答してください。`






