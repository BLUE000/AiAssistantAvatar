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

---

## 5. 段階的タスク分解パイプライン ＆ 役割分離プロンプト構築 (`F-16-9`)

### 5.1 Intent判定最適化 (`analyzeAndDecomposeTasks`)
- 日時単語「今日」「明日」などの単体による Web 検索自動発火を完全廃止。
- 「予定」「スケジュール」「タスク」「カレンダー」が含まれる場合は Web 検索を自動スキップし、TaskFlow 優先ルーティングを確定。
- 「天気」「ニュース」「株価」「為替」「潮汐」などの明確な情報目的名詞が含まれる場合のみ Web 検索を発火。

### 5.2 プロンプト役割分離構築 (`formatRoleSeparatedPrompt`)
- `User` メッセージ（`finalPrompt`）には「今日の予定は？」という純粋なユーザー発言本文のみを格納。
- 事前収集データ（TaskFlow、Web検索結果、ナレッジ）はすべて `System` 指示領域（`additionalSystemPrompt` / `systemInstruction`）へ `[事前収集リファレンスデータ (現在日時: YYYY-MM-DD時点)]` として分離注入。
