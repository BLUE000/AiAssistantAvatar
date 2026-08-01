# 基本設計書 - AIClientManager ＆ ルーティングモジュール (AIClientManager.md)

## 1. 概要
`AIClientManager` は、全 AI プロバイダクライアント（さくらAI, Gemini, Groq, Mistral, Cerebras, HuggingFace, OpenRouter, Dummy）の生命周期管理、プロバイダ自動選定、およびレートリミット監視を担当するコアコンポーネントである。

---

## 2. 主要機能 ＆ コンポーネント設計

### 2.1 使用枠最大 AI 自動選定部 (`AIRouter` / `RateLimitTracker`)
- UI で全プロバイダのチェックが外されている場合、Release ビルドではダミーを稼働させず、`RateLimitTracker` から現在利用可能かつリミット残数（RPM/RPD）が最大の最適な AI クライアントを自動選定してルーティングする。

### 2.2 APIキー未設定 ＆ レートリミット全枯渇ガード部
- 有効な API キーが 1 つも設定されていない場合は外部リクエストを即座に遮断し、「*APIキー設定案内*」を表示する。
- 設定済みプロバイダが全てレートリミット到達時は、未設定キーの有無に応じた二分岐案内（リセットまでの残分秒 ＋ 未設定プロバイダの追加登録案内）を生成・表示する。
