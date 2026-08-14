# 基本設計書 - SearchManager ＆ RAG モジュール (SearchManager.md)

## 1. 概要
`SearchManager` は、最新 Web 情報を取得して AI への事前入力データ（RAG コンテキスト）を構成するコンポーネントである。

---

## 2. 主要機能 ＆ コンポーネント設計

### 2.1 Web 検索自動フォールバック部
- 高精度構造化 API（Tavily）を優先実行し、API キー未設定・エラー時には無料の DuckDuckGo スクレイピング処理へ自動フォールバックする。

### 2.2 マークダウンナレッジエンジン (`MarkdownTableEngine`)
- ローカル Markdown ファイル群から、ユーザーの入力キーワードに合致するトリガーテーブルデータを高速抽出し、RAG コンテキストへ注入する。
- 独立フォルダ構造（`knowledge/Omikuji/`, `knowledge/Zodiac/` 等）を自動検出し、フォルダ単位での機能有効化・無効化をサポートする。
- プロンプトや Markdown 内に埋め込まれたマクロ式（`DailyTableSelect`, `TableSelectRandom`, `TableSearch`, `DailyRandom`, `Random`）およびプレースホルダー（`{Date}`, `{User}`）を事前評価・置換する。

