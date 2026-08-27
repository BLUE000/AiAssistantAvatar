# Google Gemini (Google AI Studio) アカウント作成・APIキー取得ガイド

Google Gemini（ジェミニ）は、Googleが提供する最先端のAIモデルです。Google AI Studioでは、高速かつ高精度な最新モデル（`gemini-2.0-flash`, `gemini-1.5-flash` 等）が**1日あたり最大1,500リクエストの完全無料枠**で提供されています。

---

## 🔑 APIキーの取得手順

### ステップ①：Google AI Studio へアクセス
[Google AI Studio (https://aistudio.google.com/)](https://aistudio.google.com/) にアクセスします。

### ステップ②：Google アカウントでログイン
お持ちの Google アカウント（Gmail アカウント等）でログインし、利用規約に同意します。

### ステップ③：APIキーの作成
1. 画面左上（または左メニュー）にある「**Get API key**」（API キーを取得）ボタンをクリックします。
2. 「**Create API key**」（API キーを作成）ボタンを押します。
3. プロジェクトの選択画面が表示されたら、「**Create API key in new project**」（新しいプロジェクトで API キーを作成）を選択します。

### ステップ④：キーのコピー
生成された API キー（`AIzaSy...` で始まる文字列）の横にあるコピーボタンをクリックしてコピーし、アプリの設定画面の **「Gemini API キー」** 欄に貼り付けます。

---

## 💡 アプリ設定画面での指定
- **有効**: チェックを入れる
- **Gemini API キー**: コピーしたキー（`AIzaSy...`）を貼り付け
- **モデル**: 無料枠が最も広く（15 RPM / 1,500 RPD）最速応答の `gemini-2.0-flash` が自動設定されます。（※手動で変更したい場合は設定ファイル `local_settings.json` の `"gemini_model"` を編集可能）
