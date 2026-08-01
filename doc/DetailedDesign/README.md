# 詳細設計書 - AI Assistant Avatar (DetailedDesign/README.md)

## 1. 概要
本ドキュメントは、AI Assistant Avatar システムの詳細クラス設計、シーケンス図、アルゴリズム、および各種設計ドキュメントへのインデックスを定義する。

保守性および可読性を維持するため、詳細設計情報は機能モジュールごとに以下の個別ドキュメントへ分割・管理されている。

---

## 📄 機能別 詳細設計書 インデックス

| モジュール分類 | 設計書ファイル名 | 概要・主な設計内容 |
| :--- | :--- | :--- |
| **AI モジュール** | [AI.md](file:///d:/prog/C++/AiAssistantAvatar/doc/DetailedDesign/AI.md) | `IAIClient` 派生プロバイダ群 (Sakura, Gemini, Groq, Mistral, Cerebras, HuggingFace, OpenRouter), `AIClientManager`, `RateLimitTracker`, 使用枠最大自動選定, Intent判定最適化, 役割分離プロンプト構築 |
| **検索 ＆ RAG** | [Search.md](file:///d:/prog/C++/AiAssistantAvatar/doc/DetailedDesign/Search.md) | `SearchManager` (Tavily, DuckDuckGo), Tavily➔DuckDuckGo 自動フォールバック, HTMLパース・ノイズ除去, `MarkdownTableEngine` |
| **予定管理** | [TaskFlow.md](file:///d:/prog/C++/AiAssistantAvatar/doc/DetailedDesign/TaskFlow.md) | TaskFlow スケジュール連携 API, 特定個人ドメイン接続防止安全ガード (`m_taskFlowApiUrl` 未設定保護), スケジュール JSON パース |
| **Twitch モジュール** | [Twitch.md](file:///d:/prog/C++/AiAssistantAvatar/doc/DetailedDesign/Twitch.md) | `TwitchReader` (IRC 通信), Twitch Watchdog (サイレント切断探知), Twitch OAuth 認証, `TwitchHelixClient`, レイドシャウトアウトハイブリッド送信 (`F-22-1`) |
| **Discord モジュール** | [Discord.md](file:///d:/prog/C++/AiAssistantAvatar/doc/DetailedDesign/Discord.md) | `DiscordReader` (Gateway WebSocket 通信, Heartbeat), メッセージ受信, チャンネルレスポンスルーティング |
| **UI ＆ 表示** | [UI.md](file:///d:/prog/C++/AiAssistantAvatar/doc/DetailedDesign/UI.md) | `AvatarWindow` (ウィンドウ透過・ドロップドラッグ), `HistoryViewerDialog` (会話履歴ビューア), `RateLimitTabWidget` (レートリミット管理専用タブ・動的項目カード描画), TrustChain |
