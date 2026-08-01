# 基本設計書 - AI Assistant Avatar (BasicDesign/README.md)

## 1. 概要
本ドキュメントは、AI Assistant Avatar システムの全体基本設計、アーキテクチャ方針、モジュール構成、および各種設計ドキュメントへのインデックスを定義する。

保守性および可読性を維持するため、基本設計情報は機能コンポーネントごとに以下の個別ドキュメントへ分割・管理されている。

---

## 📄 機能別 基本設計書 インデックス

| 機能コンポーネント | 設計書ファイル名 | 概要・主な設計内容 |
| :--- | :--- | :--- |
| **AIクライアント管理** | [AIClientManager.md](file:///d:/prog/C++/AiAssistantAvatar/doc/BasicDesign/AIClientManager.md) | `AIClientManager`, `AIRouter` (使用枠最大自動選定 / 未設定ガード / 全枯渇二分岐案内), `RateLimitTracker` |
| **Web検索 ＆ RAG** | [SearchManager.md](file:///d:/prog/C++/AiAssistantAvatar/doc/BasicDesign/SearchManager.md) | `SearchManager` (Tavily, DuckDuckGo), 自動フォールバック, ノイズ除去, `MarkdownTableEngine` |
| **プロンプト構築** | [PromptBuilder.md](file:///d:/prog/C++/AiAssistantAvatar/doc/BasicDesign/PromptBuilder.md) | Intent 判定最適化 (日時単体発火全廃), 段階的タスク分解, 役割 (Role) 分離プロンプト構築 (`F-16-9`) |
| **予定管理** | [TaskFlow.md](file:///d:/prog/C++/AiAssistantAvatar/doc/BasicDesign/TaskFlow.md) | TaskFlow 予定管理システム独立連携, 特定個人ドメイン接続防止安全ガード (`F-20-1`) |
| **UI ＆ アバター** | [UI.md](file:///d:/prog/C++/AiAssistantAvatar/doc/BasicDesign/UI.md) | `AvatarWindow`, 会話履歴ビューア (`HistoryViewerDialog`), レートリミット管理専用タブ (`RateLimitTabWidget` - `F-16-10`) |
| **設定 ＆ 永続化** | [Settings.md](file:///d:/prog/C++/AiAssistantAvatar/doc/BasicDesign/Settings.md) | `local_settings.json` データ永続化構造, 旧「プロバイダ制限設定」領域の全廃・一元化 |

---

## 2. 全体アーキテクチャ概要

```text
+---------------------------------------------------------------------------------+
|                                  ユーザーUI (AvatarWindow)                     |
|  - アバター透過描画   - 吹き出し表示   - 会話履歴ビューア   - レートリミット専用タブ  |
+---------------------------------------------------------------------------------+
                                         |
                                (AppEvent イベントバス)
                                         |
+---------------------------------------------------------------------------------+
|                                 AIClientManager                                 |
|  - AIRouter (自動選定) - RateLimitTracker - Intent評価/Task分解 - Role分離Prompt|
+---------------------------------------------------------------------------------+
       |                         |                        |                     |
[ 外部AIプロバイダ群 ]    [ SearchManager ]        [ TaskFlow API ]      [ チャット連携 ]
(Sakura/Gemini/Groq/   (Tavily / DuckDuckGo    (URL未設定安全ガード)  (Twitch IRC / Helix
 Mistral/Cerebras/      フォールバックRAG)                               Discord Gateway)
 HuggingFace/OpenRouter)
```
