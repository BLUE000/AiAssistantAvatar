# HuggingFace アカウント作成・API Token 取得ガイド

HuggingFace（ハギングフェイス）は、世界最大級のオープンソースAIコミュニティであり、最新のオープンLLM（Llama 3.1、Qwen 2.5等）をサーバー不要の Serverless Inference API 経由で手軽に利用できます。

---

## 🔑 Access Token（APIキー）の取得手順

### ステップ①：公式サイトへアクセス
[HuggingFace 公式サイト](https://huggingface.co/) にアクセスします。

### ステップ②：アカウント作成・ログイン
右上にある「**Sign Up**」を押して無料でアカウントを作成し、ログインします。

### ステップ③：Access Tokens 画面を開く
1. 右上のご自分のプロフィールアイコンをクリックし、「**Settings**」を選択します。
2. 左側メニューの「**Access Tokens**」を選択します。
3. 「**Create new token**」ボタンを押します。

### ステップ④：Token の設定と生成
1. **Token name**：任意の名前（例: `AiAvatarToken`）を入力します。
2. **Token type**：`Read` または `Fine-grained`（Inference 権限）を選択します。
3. 「**Generate a token**」をクリックします。

### ステップ⑤：Token のコピー
生成されたトークン文字列（`hf_...` で始まる文字列）をコピーし、アプリの設定画面の **「HuggingFace API キー」** 欄に貼り付けます。

---

## 💡 アプリ設定画面での指定
- **HuggingFace 使用**: チェックを入れる
- **HuggingFace API キー**: コピーした `hf_...` トークンを貼り付け
- **HuggingFace モデル**: プルダウンから `meta-llama/Llama-3.1-8B-Instruct`（推奨）または `Qwen/Qwen2.5-7B-Instruct` 等を選択
