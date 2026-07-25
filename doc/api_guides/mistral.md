# Mistral AI アカウント作成・APIキー取得ガイド

Mistral AI（ミストラル AI）は、バランスに優れ、自然で賢い日本語会話が可能なヨーロッパ発の先進的AIエンジンです。

---

## 🔑 APIキーの取得手順

### ステップ①：プランの確認
まず [Mistral AI 料金プランページ](https://mistral.ai/pricing/) にアクセスし、提供されているプラン（無料枠・従量課金プラン等）を確認します。

### ステップ②：コンソールにログイン
[Mistral AI Console](https://console.mistral.ai/) にアクセスし、アカウント登録（Sign Up）またはログインします。

### ステップ③：APIキーの作成画面へ
ログイン後、左側のサイドメニューから「**API Keys**」を選択し、画面右上の「**Add a new key**」ボタンをクリックします。

### ステップ④：キー設定項目の入力
表示される設定画面で以下のように指定します：
* **Key name**：任意の名称を設定します（例: `AiAssistantAvatar` など分かりやすい名前）。
* **Expiration**：`No expiration date`（期限なし）を指定します。
* **Connector access scope**：`Private and shared connectors` を設定します。

### ステップ⑤：キーの作成とコピー
「**New key**」ボタンを押すと作成された API キー（`nm-...` で始まる長い文字列）が表示されます。
キー文字列をコピーし、アプリの設定画面の **「Mistral API キー」** 欄に貼り付けます。

---

## 💡 アプリ設定画面での指定
- **Mistral AI 使用**: チェックを入れる
- **Mistral API キー**: コピーしたキーを貼り付け
