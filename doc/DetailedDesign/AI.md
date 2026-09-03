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

### 3.5 `HuggingFaceAIClient` (HuggingFace Inference API)
- エンドポイント: `https://api-inference.huggingface.co/models/...`

### 3.6 `OpenRouterAIClient` (OpenRouter API)
- エンドポイント: `https://openrouter.ai/api/v1/chat/completions`

### 3.7 `DummyAIClient` (開発・テスト用ダミーAI)
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
  > `本アプリには、各AIプロバイダ（Groq, Mistral, Sakura AI, HuggingFace, OpenRouter等）のレートリミット使用枠（RPM/RPD）や残量、リセット時間をリアルタイム監視・更新・表示する『レートリミット』タブ機能が実装されています。ユーザーからレートリミットの表示や更新について尋ねられた場合は、アプリの『レートリミット』タブからいつでも確認・更新できる旨を正しく回答してください。`

---

## 9. Groq 対策 ＆ AI リクエスト送出遅延 (時差) 詳細設計

### 9.1 送出遅延タイマー (`m_requestDelayTimer`)
- `AIClientManager::on_requestAI` および `AIClientManager::evaluateWithManagerAI` の呼び出し時、リクエスト処理を即時実行せず `600ms` の SingleShot 遅延タイマー（`QTimer::singleShot(600, ...)`）を挿入する。
- 1 秒未満のわずかな時差（遅延）を保持することで、Groq 等の外部 API サービス側で「短時間の自動スパム/Botアクセス」と判断されるのを確実に防ぐ。

---

## 10. マネージャー AI 使用プロバイダの優先度自動調整 ＆ Mistral RPM 詳細設計

### 10.1 `AIClientManager::buildFallbackProviderList` の改善
- リスト構築時、標準優先度順序（`groq`, `mistral`, `huggingface`, `openrouter`, `sakura`）から `m_provider`（選択中 Worker）に加えて、**`m_managerEnabled` が true の場合の `m_managerProvider`** も上位候補から除外/移動する。
- `m_managerProvider` はフォールバックリストの最末尾（最下位優先度）へ配置され、メイン会話プロバイダが利用可能である限り、マネージャー用プロバイダへ会話リクエストが重複して消費されるのを防ぐ。

### 10.2 Mistral AI デフォルト RPM の修正
- `MistralAIClient::getStatus` および `AvatarWindow::onFetchModelSpecsClicked` 内での `s.rpmMax` デフォルト設定値を `1` から `30` に変更する。

---

## 11. 話者・対話コンテキスト管理システム詳細設計 (Speaker & Context Management)

### 11.1 メタデータ構造体 `SpeakerContext`
```cpp
struct SpeakerContext {
    QString speaker;       // 発言者名 ("blue002", "視聴者A", "ぶるたろう", "システム")
    QString target;        // 宛先名 ("ぶるたろう", "blue002", "全体")
    enum Category {
        Self,              // AI自身の過去応答
        Streamer,          // 配信者の発言 (マイク/STT)
        LiveChat,          // 視聴者からの配信チャットコメント (Twitch/Discord)
        SystemNotice       // システム自動通知
    } category;
};
```

### 11.2 プロンプトタグ整形 `formatSpeakerTaggedPrompt`
`on_requestAI` および `m_chatHistory` 追加時、メッセージテキストの冒頭に以下の形式で構造化タグを動的挿入する：
`[発言者: {speaker} ({category}) | 宛先: {target}] {prompt}`

### 11.3 システムプロンプトへの分析ガイドライン注入
`system` プロンプトへ以下を常時追加注入する：
> `[Speaker Context Guideline]`
> `メッセージ冒頭の [発言者: X | 宛先: Y | 種別: Z] タグを分析し、誰が誰に話しかけているか正確に把握してください。`
> `視聴者が配信者に対して指摘した内容を、配信者自身の誤りや問題として誤認しないでください。`

---

## 12. JSON コメント除去ユーティリティ (`JsonCommentRemover`) 詳細設計

### 12.1 アルゴリズム概要 (`stripHashComments`)
- **関数の入力**: `QByteArray` または `QString` の RAW JSON 文字列。
- **スキャン状態フラグ**: `inString` (文字列内部か), `escaped` (エスケープ `\` の直後か)。
- **処理ロジック**:
  - 行（`\n`）単位で処理し、文字を 1 文字ずつ走査する。
  - `inString == false` の状態で `#` を検出した場合、その位置から行末までの文字を破棄（コメント除去）する。
  - ダブルクォーテーション `"` の通過時は `escaped` フラグに従って `inString` を反転し、文字列リテラル内部の `#`（例: `"api_key": "abc#123"`）は消去せず文字として保持する。
- **適用対象**: `local_settings.json` のロードを行っているすべての呼び出し箇所（`AvatarWindow`, `AIClientManager`, `TwitchReader`, `DiscordReader`, `main.cpp`）。

---

## 13. レートリミット自動復帰補正詳細設計 (`RateLimitTracker::updateAvailable`)

### 13.1 補正ロジック
`RateLimitTracker::updateAvailable` 内において、`s.nextResetAt.isValid() && s.nextResetAt <= nowUtc` の条件を満たした際：
1. `rpmMax > 0` の場合は `rpmRemaining = rpmMax` に復帰、`rpmMax <= 0` (-1) の場合は `rpmRemaining = -1` に維持/設定する。
2. `rpdMax > 0` の場合は `rpdRemaining = rpdMax` に復帰、`rpdMax <= 0` (-1) の場合は `rpdRemaining = -1` に維持/設定する。
3. `s.nextResetAt = QDateTime()` にてリセット時刻をクリアする。
4. `available` 判定条件を `rpmOk && rpdOk && tpmOk` の真偽値へ補正し、残枠保持中は `available = true` を維持する。

### 13.2 ローカル減算時のタイマー起動 (`recordLocalConsumption`)
- `rpmRemaining` または `tpmRemaining` が全枠（`rpmMax` / `tpmMax`）から初回減算された際、`s.nextResetAt` が未設定であれば自動的に `QDateTime::currentDateTimeUtc().addSecs(60)` を設定し、60秒後の全量自動再補充タイマーを開始する。

### 13.3 未初期化残量 (`-1`) 評価論理式
`RateLimitTracker::updateAvailable` 内における各 `Ok` 判定論理式を以下のように評価補正する：
- `rpmOk = (s.rpmMax <= 0) || (s.rpmRemaining == -1) || (s.rpmRemaining > 0);`
- `rpdOk = (s.rpdMax <= 0) || (s.rpdRemaining == -1) || (s.rpdRemaining > 0);`
- `tpmOk = (s.tpmMax <= 0) || (s.tpmRemaining == -1) || (s.tpmRemaining > 0);`
- `tpdOk = (s.tpdMax <= 0) || (s.tpdRemaining == -1) || (s.tpdRemaining > 0);`
---

## 14. 宛先・話者変数の初期化 ＆ 翻訳コマンド前置正規化詳細設計

### 14.1 応答完了時・中断時の状態変数クリーンアップ (`clearRequestState`)
- `AIClientManager::on_clientRequestFinished` の出口（正常応答通知時、エラー発生時、タイムアウト時、翻訳処理時問わず）において、各リクエストスコープの変数をリセットする：
  ```cpp
  m_currentDiscordChannelId.clear();
  m_currentTwitchChannel.clear();
  m_currentRequester.clear();
  ```
- これにより、次回リクエスト処理時に前回の Twitch/Discord 返信先や話者識別子が意図せず残存・リークすることを完全に防ぎ、UI画面直接入力に対する応答が Twitch へ誤送信される障害および人違いの発生を防止する。

### 14.2 翻訳コマンド前置記号・ウェイクワード正規化アルゴリズム
- `processRequest` における翻訳判定前処理：
  ```cpp
  QString transCheckPrompt = trimmedPrompt;
  static const QRegularExpression prefixRegex("^(?:!ai|/ai|!|/)\\s*", QRegularExpression::CaseInsensitiveOption);
  transCheckPrompt.remove(prefixRegex);
  transCheckPrompt = transCheckPrompt.trimmed();

  if (transCheckPrompt.startsWith("trans", Qt::CaseInsensitive)) {
      m_isTranslationRequest = true;
      // "trans" 以降の引数を言語・テキストとしてパース
  }
  ```
- `!ai trans en こんにちは` や `/ai trans こんにちは` などの表記ゆれ入力に対し、前置記号を除去・正規化して評価することにより、100% 確実に翻訳処理（`m_isTranslationRequest = true`）を起動する。

---

## 15. Web検索・外部知識応答における情報量制御詳細設計 (F-34)

### 15.1 回答モード列挙型 (`ResponseDetailMode`)
```cpp
enum class ResponseDetailMode {
    Short,      // デフォルト: 1〜3文程度、結論・主要因優先、簡潔
    Normal,     // 中程度: 数段落程度、平易な専門用語・理由解説付き
    Detailed    // 詳細: 長文詳細解説、専門用語・数値・背景・例外を含む
};
```

### 15.2 意図判定アルゴリズム (`determineResponseDetailMode`)
ユーザーの入力テキスト（および直前の対話履歴）から回答モードを判定する：
1. **`Detailed` 判定**:
   - 入力に「詳しく」「詳細に」「もっと教えて」「仕組み」「専門的に」「具体的に」「なぜそうなるのか詳しく」「数字も」等が含まれる場合。
2. **`Short` 判定 (簡潔化 ＆ ユーザー指摘による粒度縮小)**:
   - 簡潔化要求: 「簡単に」「一言で」「短く」「ざっくり」「要点だけ」「結論だけ」が含まれる場合。
   - **過度な詳細指摘**: 「細かすぎる」「細かい」「詳しすぎる」「長すぎる」「もっと短く」「要点だけでいい」等の指摘表現が含まれる場合。
3. **デフォルト**:
   - 上記キーワードが含まれない通常の質問・調査依頼に対しては `Short` をデフォルト適用する。

### 15.3 プロンプト指示インジェクション
Web 検索（`WebSearchRAG`）または知識検索結果が存在する場合、`additionalSystemPrompt` へ以下の指示ブロックを動的注入する：

- **`Short` モード時**:
  ```text
  【回答の情報量・長さの指示】
  - 配信中の視聴者が素早く理解できるよう、1〜3文程度の簡潔な文章で回答してください。
  - 最も重要な結論や理由を最優先で伝え、専門用語や細かい数値・不要な背景説明は極力省略してください。
  ```
- **ユーザーから「細かすぎる/長すぎる」と指摘された場合の粒度縮小・言い直し指示**:
  ```text
  【回答の粒度修正指示】
  - ユーザーから説明が細かすぎる・長すぎるとの指摘を受けました。直前の内容を反省し、最も伝えたい要点のみを 1〜2 文程度にギュッと凝縮して、平易な言葉で言い直してください。
  ```
- **`Normal` モード時**:
  ```text
  【回答の情報量・長さの指示】
  - 数段落程度で、理由や背景を含めて分かりやすく説明してください。
  - 専門用語を使用する場合は簡単な解説を添えてください。
  ```
- **`Detailed` モード時**:
  ```text
  【回答の情報量・長さの指示】
  - ユーザーが詳細な説明を求めているため、背景、仕組み、専門用語、具体的な数値や条件を含めて詳しく包括的に解説してください。
  ```

---

## 16. ニックネーム登録判定・誤認防止および利用案内詳細設計 (F-11)

### 16.1 誤認防止判定ルール
- **対象ケース（登録対象）**:
  - 発言者自身が主語として明示的に名乗った場合（例:「〇〇です」「〇〇だよ」「私の名前は〇〇」「〇〇と呼んで」「〇〇って呼んでね」等）。
  - 配信主が特定ユーザーの呼び名を明示的に指定した場合（例:「〇〇さんの呼び名を✕✕にして」等）。
- **非対象ケース（誤認防止・ツール呼び出し禁止）**:
  - 文脈中で第三者、配信者、または話題の対象として名前が登場した場合（例:「〇〇さんにおすすめのキャラは？」「〇〇さんはどう思う？」「〇〇さんの配信面白い」等）。
  - 上記の非対象ケースでは、`update_nickname` ツールの呼び出しを行わず、またニックネーム設定と誤認した確認発言（例:「〇〇と呼んでほしいってことかな？」）を行ってはならない。

### 16.2 システムプロンプト指示の厳格化
各AIクライアント（`MistralAIClient`, `GroqAIClient` 等）および `AIClientManager` で注入するシステムプロンプト指示を以下のように厳格化する：
> `ユーザー自身が「〇〇です」「〇〇だよ」と名乗る自己紹介や、「〇〇と呼んで」などの呼び名指定をした場合のみ、『update_nickname』ツールを呼び出してニックネームを設定してください。文脈中に第三者や配信者の名前が登場しただけの場合（例：「〇〇さんにおすすめの〜」など）は、ニックネーム設定と誤認しないでください。`

### 16.3 ツール定義 (`update_nickname`) の明確化
`update_nickname` ツールの `description` をより厳格かつ明確に定義する：
> `Register or update a nickname or preferred name for a user. Call this ONLY when the user explicitly specifies how they themselves want to be called (e.g. 'Call me X', 'My name is X', 'Xと呼んで', 'Xです'). Do NOT call this for mentions of other people or general context.`

### 16.4 ユーザーからの使い方案内に対する応答
- ユーザーからニックネーム機能や呼び名の登録方法について尋ねられた場合（例:「ニックネームってどうやって登録するの？」「名前変えられる？」等）は、ツールを実行せず、自然な対話で設定方法（「『〇〇と呼んで』とチャットで教えてくれたら、その呼び名で呼ぶように設定するよ！」等）を親切に回答する。

---

## 17. レイド歓迎プロンプトおよびシャウトアウト生成詳細設計 (F-22)

### 17.1 レイド歓迎プロンプト文脈指示
`AIClientManager::handleRaidShoutout` において、AI クライアントへ送信する紹介文生成プロンプトを以下のように構成する：

```text
あなたは配信アバターです。相手クリエイター「%1」さんが、ご自身の配信を終えてリスナーの皆さんを引き連れて私たちの配信へ遊びに来てくれました（レイドしてくれました）。
相手クリエイター「%1」さんと一緒に来てくれたリスナーの皆さんを温かく歓迎し、レイドのお礼を伝えつつ、相手の魅力を私たちの視聴者に紹介するコメントを作成してください。

【クリエイター情報】
- Twitch ID / 表示名: %2 / %1
- 自己紹介 (Bio): %3
- 直近の配信ゲーム/カテゴリ: %4
- 配信タイトル: %5
- 公式SNS/外部情報: %6

【重要・出力条件】
- 私たちが相手の配信枠を見に行く（レイドする）のではなく、「相手が私たちの配信枠へレイドして来てくれた」という状況です。逆の立場（相手の配信を見に行こう等）と絶対に誤認しないでください。
- レイドして来てくれたことへの温かい感謝と歓迎を述べ、相手のチャンネルの魅力紹介やフォロー推奨を行ってください。
- 長さ: %7
- トーン・口調: %8
```

### 17.2 入力ソースおよび送信先チャンネルの確定
レイドイベント受信時：
- `m_currentSource = "Twitch"`
- `m_currentTwitchChannel = m_twitchChannel`
- `m_isShoutoutRequest = true`
を設定し、応答生成完了時に `event.source = "Twitch"`, `event.extraData["twitch_channel"] = m_twitchChannel` が確実に付与されて Twitch チャットへ自動送信されるようにする。

### 17.3 `TwitchHelixClient` 連携およびテスト容易性設計
`AIClientManager` は内部の `TwitchHelixClient` インスタンスを `setHelixClient(TwitchHelixClient *client)` 経由でテスト用スタブ/モックに差し替え可能とする。
これにより、単体テスト環境においてもレイドイベント受信から `/shoutout` REST API 発火、クールタイム待機キュー、チャット送信、フォロー推奨メッセージ送信までの E2E シーケンスを網羅的に検証可能とする。

### 17.4 コンソールアプリ (`TwitchIntroGenerator`) への委譲とフォールバック
`AIClientManager` は、レイド時および会話紹介時の「クリエイター情報収集 ＋ プロンプト構築 ＋ AI紹介文生成」について、`TwitchIntroGenerator.exe` を非同期サブプロセス（`QProcess`）として起動して実行する。
- **実行引数例**:
  - `TwitchIntroGenerator.exe --user <login> --mode raid --length <m_shoutoutLength> --tone <m_shoutoutTone>`
- **終了時ハンドリング**:
  - 正常終了（ExitCode 0）: 標準出力のテキストを `applyMask()` 等のフィルタを通過させたうえで Twitch 送信用イベント（`AIResponseReceived`）として発火。
  - タイムアウト（15秒）または異常終了: ログ出力のうえ、デフォルトのお礼メッセージ（`「〇〇さん、レイドありがとうございます！」` 等）をフォールバックとして出力。
- **`/shoutout` API 制御の分離**:
  - Twitch 公式 `/shoutout` REST API の送信および 120 秒クールタイム待機キュー管理は、CLI 呼び出しと並行して `AIClientManager` が直接 `TwitchHelixClient` を制御して実行する。

---

## 18. `MarkdownTableEngine` 除外トリガー（ネガティブキーワード）仕様 ＆ 占い・おみくじ想起設計 (F-15)

### 18.1 背景・課題
- 従来は `# トリガー` による部分一致判定のみを行っていたため、例えば「おみくじ」に「占い」を登録すると、「タロット占い」「手相占い」「星座占い」等の別の占い要求に対しても「占い」の部分一致でおみくじナレッジが誤発火してしまう問題があった。

### 18.2 除外トリガー（`# 除外トリガー` / `# Exclude Triggers`）仕様
Markdown ナレッジファイルに除外トリガーリストを定義可能とする。

```markdown
# トリガー
- おみくじ
- 運試し
- うらない
- 占い
- 今日の占い

# 除外トリガー
- 星座
- 星
- タロット
- 手相
- 血液型
- 四柱推命
- 姓名判断
```

### 18.3 判定アルゴリズム (`MarkdownTableEngine::resolveBestEntryForTrigger`)
1. **除外判定（最優先）**:
   - 入力プロンプトに対象エントリの `excludeTriggers` に含まれる文字列が1件でも部分一致（大文字小文字無視）した場合、そのエントリのスコア計算を即座に破棄（スキップ）する。
2. **正トリガー判定**:
   - 除外判定を通過したエントリに対し、従来通り完全一致（+1000点）および部分一致（文字長×10点 ＋ マッチ件数×50点 ＋ 優先度）を評価し、最高スコアのエントリを抽出する。

### 18.4 想定される動作結果
| ユーザー入力 | おみくじ (Omikuji) | 星座占い (Zodiac) | 判定結果 | AIへの注入コンテキスト |
| :--- | :--- | :--- | :--- | :--- |
| 「うらない」 | トリガー一致 / 除外なし | 不一致（星座なし） | **Omikuji** | 当日の確定おみくじ結果（大吉・小吉等） |
| 「今日の占いして」 | トリガー一致 / 除外なし | 不一致（星座なし） | **Omikuji** | 当日の確定おみくじ結果 |
| 「ふたご座のうらない」 | **除外**（「座」検出） | トリガー一致（「ふたご座」） | **Zodiac** | 双子座の確定占い結果（順位・アイテム等） |
| 「タロット占いして」 | **除外**（「タロット」検出） | 不一致 | **なし**（自由対話） | ナレッジ注入なし（AIが自然な対話で返答） |
| 「手相占いできる？」 | **除外**（「手相」検出） | 不一致 | **なし**（自由対話） | ナレッジ注入なし（AIが自然な対話で返答） |

---

## 19. コンソールアプリ共通プロセス起動 ＆ `tools/` パス探索・DLL共有仕様

### 19.1 概要・目的
メインアプリからサブプロセスとして呼び出される各種コンソールアプリ（`WebSearcher.exe`, `CommunityObserver.exe`, `TwitchIntroGenerator.exe` 等）は、配布パッケージ内で `tools/` サブフォルダに隔離・集約される。
これに伴い、メインアプリ（`SearchManager`, `AIClientManager`, `CommunityObserverEngine` 等）における実行ファイル探索およびプロセス起動処理を共通仕様として標準化する。

### 19.2 探索優先順位 (`resolveExecutablePath`)
コンソールアプリの実行ファイルパスを解決する際、以下の順序で探索を行う：

1. `QCoreApplication::applicationDirPath() + "/tools/<app_name>.exe"`（配布環境・リリース環境最優先）
2. `QCoreApplication::applicationDirPath() + "/<app_name>.exe"`（従来互換・同一フォルダ）
3. `QCoreApplication::applicationDirPath() + "/build/<app_name>.exe"`（開発環境・同一ディレクトリ）
4. `QDir::currentPath() + "/build/<app_name>.exe"`（開発環境カレント）
5. `QDir::currentPath() + "/<app_name>.exe"`
6. `"<app_name>.exe"`（環境変数 PATH）

### 19.3 DLL 共有アーキテクチャ（`PATH` 環境変数自動注入）
`tools/` サブディレクトリ内のコンソールアプリが、ルート階層に配置された Qt6 共有 DLL（`Qt6Core.dll` 等）および MinGW ランタイム DLL を確実にロードできるようにするため、`QProcess` 起動時にメインアプリ側で環境変数を自動設定する。

```cpp
// 共通プロセス起動ヘルパー
void configureProcessEnvironment(QProcess &process) {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString appDir = QCoreApplication::applicationDirPath();
    // ルートフォルダを PATH の先頭に追加することで、tools/ 配下の EXE がルートの DLL を参照可能
    env.insert("PATH", appDir + ";" + env.value("PATH"));
    process.setProcessEnvironment(env);
}
```

- **効果**:
  - `tools/` 配下に DLL を二重コピーする必要がなく、配布パッケージサイズを最小限に維持。
  - Windows の DLL 探索順序（`PATH`）により、常に同一パッケージ内の正規 Qt6/MinGW DLL が確実にロードされる。

---

## 20. クリエイター紹介文生成プロセス連携 ＆ `/shoutout` 制御仕様

### 20.1 概要
メインアプリ（`AIClientManager`）は、クリエイター紹介のトリガー種別に応じて `TwitchIntroGenerator.exe` を非同期起動し、適切なモード引数を渡す。

### 20.2 モード引数の制御
1. **レイド受信時 (`handleRaidShoutout`)**:
   - 引数: `--user <login> --mode raid --length <length> --tone <tone>`
   - `TwitchIntroGenerator` 側でレイド歓迎プロンプトによる紹介文生成と同時に、Twitch 公式 `/shoutout` REST API を自動送信する。
2. **会話・チャット紹介時 (`handleConversationShoutout`)**:
   - 引数: `--user <login> --mode conversation --length <length> --tone <tone>`
   - `TwitchIntroGenerator` 側は `/shoutout` REST API を送信せず、純粋な紹介文生成のみを実行する。

### 20.3 二重送信防止とクールタイム管理
- CLI 側で `/shoutout` が実行されるため、メインアプリ側での `/shoutout` REST API 二重送信は行わない。
- 120 秒クールタイムタイマー（`m_shoutoutCooldownTimer`）および待機キュー（`m_shoutoutQueue`）の管理はメインアプリ側で統括し、UIステータスを更新する。

---

## 21. Google Gemini (Google AI Studio) クライアント (`GeminiAIClient`) 詳細設計

### 21.1 概要・通信仕様
Google AI Studio の Gemini API を利用し、高速・高精度な応答を生成するクライアント。
OpenAI 互換エンドポイントを使用する。

- **エンドポイント**: `https://generativelanguage.googleapis.com/v1beta/openai/chat/completions`
- **認証**: HTTP ヘッダー `Authorization: Bearer <gemini_api_key>`
- **モデル選定仕様**:
  - **自動選定（デフォルト）**: 無料利用枠（15 RPM / 1,500 RPD）および応答速度が最良となる `gemini-flash-latest` または動的探索による最新 Flash モデルを自動使用。
  - **UI 仕様**: Mistral と同様に **API キー入力欄のみ** を表示し、モデル選択コンボボックスは配置しない（UIの簡素化）。
  - **設定ファイルオーバーライド**: `local_settings.json` の `"gemini_model"` キーに現在使用しているモデル名を保存・保持し、ユーザーが手動で書き換えた場合はそのモデルを適用する。
- **レートリミット（無料枠）**: RPM: 15, RPD: 1500, TPM: 1,000,000, コスト: 0.0
  - アプリ起動時および「レートリミット」タブ表示時に、Gemini プロバイダカードが自動表示され残リクエスト数が追従更新される。

### 21.2 リクエスト・レスポンス JSON 構造
```json
// Request
{
  "model": "gemini-flash-latest",
  "messages": [
    { "role": "system", "content": "システム指示テキスト..." },
    { "role": "user", "content": "ユーザー質問..." }
  ],
  "temperature": 0.7,
  "max_tokens": 1024,
  "stream": false
}

// Response
{
  "id": "chatcmpl-...",
  "choices": [
    {
      "index": 0,
      "message": {
        "role": "assistant",
        "content": "AI回答テキスト..."
      },
      "finish_reason": "stop"
    }
  ]
}
```

---

## 22. Gemini コンソールアプリ (`GeminiChatter`) 詳細設計

### 22.1 概要・責務
`GeminiChatter` は、Google Gemini API をスタンドアロン環境や外部プロセスから呼び出す独立 CLI ツールである。
配布環境では `tools/GeminiChatter.exe` に配置される。

### 22.2 コマンドライン引数仕様
```text
GeminiChatter [options]
Options:
  --prompt <text>       [必須] 入力プロンプト・質問テキスト
  --system <text>       [任意] システムプロンプト指示
  --model <model>       [任意] Gemini モデル名 (デフォルト: "gemini-flash-latest")
  --api-key <key>       [任意] Gemini API キー (省略時は設定ファイルから取得)
  --config <path>       [任意] 設定ファイルパス (デフォルト: "Config/local_settings.json")
  --format <format>     [任意] 出力形式 ("text" または "json", デフォルト: "text")
  --timeout <ms>        [任意] タイムアウトミリ秒 (デフォルト: 15000)
  --help, -h            ヘルプ表示
```

### 22.3 内部処理シーケンス
1. **引数および設定ロード**:
   - 引数 `--api-key` が未指定の場合、`--config` で指定された `local_settings.json` の `"gemini_api_key"` をロード。
---

## 23. Manager AI 文脈判定エンジン (Context Evaluator) ＆ 会話行為分類・聞き返し詳細設計 (F-40)

### 23.1 概要・責務
`ManagerContextEvaluator` は、複数ユーザーが発言する配信チャットにおいて、発言の宛先・会話行為（Speech Act）・指示語の参照先・確信度を判定するモジュールである。

### 23.2 データ構造定義
```cpp
struct ChatMessageEntry {
    QString messageId;
    QString sender;
    bool isAssistant = false;
    QString text;
    qint64 timestamp = 0;
};

struct ContextCandidate {
    QString messageId;
    QString sender;
    bool isAssistant = false;
    QString text;
    int ageSeconds = 0;
};

struct PendingClarification {
    QString requester;
    QString candidateTopic;
    QString questionText;
    qint64 timestamp = 0;
    bool isValid(qint64 currentMs, qint64 timeoutMs = 60000) const {
        return (currentMs - timestamp) <= timeoutMs;
    }
};

struct ManagerContextResult {
    QString target = "ASSISTANT";            // "ASSISTANT", "USER", "OTHER"
    QString speechAct = "QUESTION";          // "INFORMATION", "CORRECTION", "QUESTION", "COMMAND", "OPINION_DISAGREEMENT", "SUGGESTION", "REACTION", "OTHER"
    QString refMessageId;                    // 参照先メッセージID
    double referenceConfidence = 1.0;        // 0.0 〜 1.0
    QString responseAction = "ANSWER";       // "ANSWER", "ACKNOWLEDGE", "CORRECT_APOLOGY", "ASK_CLARIFICATION", "IGNORE"
};
```

### 23.3 Manager AI 判定プロンプト ＆ 入出力 JSON 仕様

#### Manager AI システムプロンプト方針
- **基本原則**:
  - 指示語（これ・それ・そこ等）の参照先を無理に推測して決定してはならない。参照先を特定できないことは正常な結果である。
  - 「正解を出すこと」よりも「誤った対象を参照して的外れな回答を行わないこと」を優先する。
  - 候補がない場合、または複数の候補が存在し会話ログから十分な確信を持って 1 つに絞り込めない場合は、`reference_message_id` を `null` とし、`response_action` に `ASK_CLARIFICATION` を指定する。

#### Manager AI 入力プロンプト
```json
{
  "current_message": {
    "sender": "userB",
    "text": "アバター名、そこは静岡だよ！"
  },
  "candidates": [
    { "message_id": "msg_001", "sender": "userA", "is_assistant": false, "text": "富士山ってどこにあるの？" },
    { "message_id": "msg_002", "sender": "AI", "is_assistant": true, "text": "富士山は山梨県のみに位置していますよ！" },
    { "message_id": "msg_003", "sender": "userC", "is_assistant": false, "text": "昨日のゲーム面白かったね" }
  ],
  "pending_clarification": null
}
```

#### Manager AI 出力 JSON
明確・高確信度時:
```json
{
  "target": "ASSISTANT",
  "speech_act": "CORRECTION",
  "reference_message_id": "msg_002",
  "reference_confidence": 0.95,
  "response_action": "CORRECT_APOLOGY"
}
```

曖昧・複数候補競合・低確信度時の出力例 (正常動作):
```json
{
  "target": "ASSISTANT",
  "speech_act": "CORRECTION",
  "reference_message_id": null,
  "reference_confidence": 0.35,
  "response_action": "ASK_CLARIFICATION"
}
```

### 23.4 Worker AI プロンプト指示制御仕様
- **`CORRECT_APOLOGY`**:
  - 指示: `【過去発言の訂正受容指示】ユーザーから過去のAI発言への訂正・指摘を受けました。一般論や人生論、励まし（周りに合わせて改善していこう等）を展開することは完全に禁止します。素直に誤りを認めて『あ、〇〇なんだ！勘違いしてた、ごめん！』のように 1〜2 文程度で簡潔に返答してください。`
- **`ACKNOWLEDGE`**:
  - 指示: `【情報伝達の受け止め指示】ユーザーはAIへの情報伝達を行っています。質問として解説するのではなく、『へー、〇〇さんはそう言ってたんだ！』のように自然な相槌・リアクションを 1〜2 文で返答してください。『〜するって』などの未来・予告を『〜した』と過去形に誤認しないでください。`
- **`ASK_CLARIFICATION`**:
  - 指示: `【聞き返し指示】発言内容の参照先が不明です。『それってどれのこと？』のように 1 文で短く確認・聞き返しを行ってください。`
- **`GREET_ON_BEHALF` (挨拶代行・COMMAND)**:
  - 指示: `【挨拶・発話の代行指示】ユーザーから配信終了の挨拶やお礼などの代行発話指示を受けました。発言者個人への労いではなく、配信の視聴者・全体に向けた挨拶（例: 『皆さん、本日の配信も見てくれてありがとうございました！また次回の配信でお会いしましょう！』）を明るく発話してください。`

### 23.5 全AIクライアント共通システムプロンプト改訂（名乗り抑制・質問即応）
各 AI プロバイダ（Sakura, Gemini, Groq, Mistral, OpenRouter, HuggingFace）の共通システムプロンプトに以下を追加・適用する：
```text
通常の対話や質問応答において、毎回自分の名前を名乗ったり自己紹介（『私は〇〇』など）を挟まないでください。自己紹介は初対面の挨拶や『名前は何？』と直接尋ねられた場合のみ行ってください。質問に対して定型的な挨拶（『今日も元気ですか？』『お手伝いがんばるよ』など）で誤魔化さず、質問内容に即してキャラクターらしく自然に回答してください。
```










### 24. モデル設定の設定ファイル管理および 404 自己修復機能仕様 (F-43)

#### 24.1 各プロバイダにおける空文字時の最適モデル自動選定仕様
1. **Gemini (`GeminiAIClient`)**:
   - デフォルト/空文字時: `gemini-flash-latest`（または `/v1beta/models` で探索した最新 Flash モデル）
2. **Groq (`GroqAIClient`)**:
   - デフォルト/空文字時: `/openai/v1/models` からアクティブな最新モデル（`llama-3.3-70b-versatile` 等）を自動選定。
3. **OpenRouter (`OpenRouterAIClient`)**:
   - デフォルト/空文字時: `/api/v1/models` からアクティブな最良 `:free` モデルを動的選定。
4. **HuggingFace (`HuggingFaceAIClient`)**:
   - デフォルト/空文字時: 推奨 Instruct モデルを動的選定。

#### 24.2 404 (Not Found / モデル廃止) 時の自動フォールバック＆リトライシーケンス
1. 各 AI クライアントが HTTP 404 を受信した場合、`m_model` が無効化されたと判定。
2. 直ちに最新モデル一覧 API を再照会し、代替となる最新の推奨モデルへ `m_model` を更新。
3. 同一リクエストを 1 回自動リトライし、成功時は正常に対話を継続する。

## 25. Mistral コンソールアプリ (`MistralChatter`) 詳細設計 (F-44)

### 25.1 概要
`MistralChatter` は、Mistral AI API をスタンドアロン環境や外部プロセスから呼び出す独立 CLI ツールである。
配布環境では `tools/MistralChatter.exe` に配置される。

### 25.2 コマンドライン構文
```text
MistralChatter [options]
```

### 25.3 オプション一覧
| オプション | 引数 | 必須/任意 | 説明 |
| :--- | :--- | :--- | :--- |
| `-p, --prompt` | `<prompt>` | **必須** | AI への入力プロンプトテキスト |
| `-s, --system` | `<system>` | 任意 | システムプロンプト（指示文） |
| `-m, --model` | `<model>` | 任意 | Mistral モデル名 (デフォルト: `mistral-small-latest`) |
| `-k, --api-key` | `<key>` | 任意 | Mistral API キー (未指定時は設定ファイルから取得) |
| `-c, --config` | `<path>` | 任意 | `local_settings.json` のパス |
| `-f, --format` | `<text\|json>` | 任意 | 出力フォーマット (デフォルト: `text`) |
| `--timeout` | `<ms>` | 任意 | タイムアウトミリ秒 (デフォルト: `15000`) |
| `-h, --help` | なし | 任意 | ヘルプ表示 |
| `-v, --version` | なし | 任意 | バージョン表示 |

### 25.4 終了コード
- `0`: 正常終了 (生成テキストを出力)
- `1`: API エラー / 認証エラー / 通信タイムアウト
- `2`: コマンドライン引数エラー (必須オプション欠落など)


## 26. AIプロバイダ高速応答保証・短縮タイムアウト＆ Gemini 2.5 Flash 正規化詳細設計 (F-45)

### 26.1 Google Gemini 正規モデル名定義
- `GeminiAIClient` のデフォルトモデルを `"gemini-2.5-flash"` と定義する。
- 空文字 `""` が設定されている場合も、自動的に `"gemini-2.5-flash"` を適用する。
- 404 受信時の自己修復フォールバック先モデルは `"gemini-1.5-flash"` とし、未知のエイリアス（`gemini-flash-latest` 等）を使用しない。

### 26.2 8秒短縮タイムアウト制御シーケンス
1. `AIClientManager::sendWorkerRequest(provider, prompt, ...)` 呼び出し時に、プロバイダ専用の 8,000ms タイマー（`m_workerTimeoutTimer`）を開始する。
2. 8,000ms 以内に `on_workerClientFinished` が発火した場合、タイマーを停止して通常どおり処理を進める。
3. 8,000ms 経過してタイマーが発火（`on_workerTimeout`）した場合：
   - 現在のクライアントに対して通信中断を要求し、レートリミットトラッカーにレイテンシ遅延を記録。
   - `buildFallbackList()` から次のプロバイダを取得し、即座に再リクエストを送信する。
   - すべてのフォールバック先がタイムアウト・エラーとなった場合のみ、ユーザーに自然なエラーメッセージ（「プロバイダの応答がタイムアウトしました」）を返却し、返答待ち状態を解除する。


## 27. GroqChatter および SakuraChatter 独立CLIツール詳細設計 (F-46)

### 27.1 ソース構成とエントリポイント
- **GroqChatter**: `src/ai/groq_chatter_main.cpp`
- **SakuraChatter**: `src/ai/sakura_chatter_main.cpp`
- 各ツールは `QCommandLineParser` を利用して引数をパースし、`local_settings.json` のキー自動補完を行う。

### 27.2 終了コード仕様
- `0`: 推論成功（テキストまたはJSONを標準出力に出力）
- `1`: APIエラー、通信エラー、タイムアウト、認証失敗
- `2`: 引数不備（`--prompt` の未指定等）


## 28. サブプロセス・CLIツール Qt プラグイン探索自動解決詳細設計 (F-47)

### 28.1 対象 CLI ツール
- `src/search/web_searcher_main.cpp`
- `src/ai/gemini_chatter_main.cpp`
- `src/ai/mistral_chatter_main.cpp`
- `src/ai/groq_chatter_main.cpp`
- `src/ai/sakura_chatter_main.cpp`
- `src/observer/community_observer_main.cpp`
- `src/twitch/twitch_intro_generator_main.cpp`

### 28.2 プラグイン初期化ロジック
- 各 CLI の `QCoreApplication app(argc, argv);` 生成直後に以下を実行:
  ```cpp
  QCoreApplication::addLibraryPath(QCoreApplication::applicationDirPath() + "/..");
  QCoreApplication::addLibraryPath(QCoreApplication::applicationDirPath());
  ```
- `ProcessUtils::configureProcessEnvironment(QProcess &process)` において:
  ```cpp
  env.insert("QT_PLUGIN_PATH", appDir + ";" + appDir + "/plugins");
  ```


## 29. Web検索結果ノイズ除去 ＆ 10秒タイムアウト ＆ 呼び名指示配置詳細設計 (F-48)

### 29.1 検索結果クレンジング処理
- `TavilySearchProvider::cleanseContent(const QString &text)`:
  - 連続する空白・改行（`\n+`, `\s{2,}`）を単一の改行/空白に正規化。
  - 「ページを表示できませんでした」「ブラウザの戻るボタン...」等のエラー定型文を除去。
  - `| 2026年 2025年...` のような連続年号・日付テーブル行を検知してトリミング。
  - 最大長 350 文字でスライス。

### 29.2 10秒タイムアウトと Abort 処理
- `AIClientManager`:
  ```cpp
  m_workerTimeoutTimer->setInterval(10000); // 10秒
  ```
- タイムアウト発生時の Abort:
  - クライアントの `QNetworkReply::abort()` を呼び出し、内部の `m_activeReply` を安全に破棄。

### 29.3 プロンプト配置順序
- 各クライアントの `sendRequest` において:
  ```cpp
  QString systemPrompt = buildBaseSystemPrompt(avatarName);
  if (!sessionContext.isEmpty()) {
      systemPrompt += "\n\n【以前の会話コンテキスト】\n" + sessionContext;
  }
  if (!systemInstruction.isEmpty()) {
      systemPrompt += "\n\n" + systemInstruction;
  }
  ```


## 30. MistralAIClient Pro モデル対応 ＆ 403/404 自己修復フォールバック詳細設計 (F-50)

### 30.1 対応モデル定義
- Pro/Paid 向けモデル: `mistral-large-latest`, `codestral-latest`, `pixtral-large-latest`
- Free/標準向けモデル: `mistral-small-latest`, `open-mistral-nemo`

### 30.2 403/404 自動降格シーケンス
- `MistralAIClient::on_networkReplyFinished(QNetworkReply *reply)`:
  ```cpp
  int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if ((httpCode == 403 || httpCode == 404) && !m_hasRetriedFallback) {
      m_hasRetriedFallback = true;
      qWarning() << "MistralAIClient:" << httpCode << "received for model" << m_model
                 << "-> Auto-fallbacking to free tier model 'mistral-small-latest' and retrying...";
      m_model = (m_model == "mistral-small-latest") ? "open-mistral-nemo" : "mistral-small-latest";
      QTimer::singleShot(0, this, [this]() {
          sendRequest(m_pendingPrompt, m_pendingHistory, m_pendingSessionContext, m_pendingSystemInstruction);
      });
      return;
  }
  ```
