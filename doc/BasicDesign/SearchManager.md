# 基本設計書 - SearchManager ＆ RAG モジュール (SearchManager.md)

## 1. 概要
`SearchManager` は、最新 Web 情報を取得して AI への事前入力データ（RAG コンテキスト）を構成するコンポーネントである。

---

## 2. 主要機能 ＆ コンポーネント設計

### 2.1 Web 検索自動フォールバック部
- 高精度構造化 API（Tavily）を優先実行し、API キー未設定・エラー時には無料の DuckDuckGo スクレイピング処理へ自動フォールバックする。

### 2.2 マークダウンナレッジエンジン (`MarkdownTableEngine`)
- ローカル Markdown ファイル群から、ユーザーの入力キーワードに合致するトリガーテーブルデータを高速抽出し、RAG コンテキストへ注入する。
