# 基本設計書 - AI Assistant Avatar (BasicDesign/README.md)

## 1. 概要
本ドキュメントは、AI Assistant Avatar システムの全体基本設計、アーキテクチャ方針、モジュール構成、および各種設計ドキュメントへのインデックスを定義する。

保守性および可読性を維持するため、基本設計情報は機能コンポーネントごとに以下の個別ドキュメントへ分割・管理されている。

---

## 📄 機能別 基本設計書 インデックス

| 機能コンポーネント | 設計書ファイル名 | 概要・主な設計内容 |
| :--- | :--- | :--- |
| **AIクライアント管理** | [AIClientManager.md](AIClientManager.md) | `AIClientManager`, `AIRouter` (使用枠最大自動選定 / 未設定ガード / 全枯渇二分岐案内), `RateLimitTracker` |
| **Web検索 ＆ RAG** | [SearchManager.md](SearchManager.md) | `SearchManager` (Tavily, DuckDuckGo), 自動フォールバック, ノイズ除去, `MarkdownTableEngine` |
| **プロンプト構築** | [PromptBuilder.md](PromptBuilder.md) | Intent 判定最適化 (日時単体発火全廃), 段階的タスク分解, 役割 (Role) 分離プロンプト構築 (`F-16-9`) |
| **予定管理** | [TaskFlow.md](TaskFlow.md) | TaskFlow 予定管理システム独立連携, 特定個人ドメイン接続防止安全ガード (`F-20-1`) |
| **UI ＆ アバター** | [UI.md](UI.md) | `AvatarWindow`, 会話履歴ビューア (`HistoryViewerDialog`), レートリミット管理専用タブ (`RateLimitTabWidget` - `F-16-10`) |
| **設定 ＆ 永続化** | [Settings.md](Settings.md) | `local_settings.json` データ永続化構造, 旧「プロバイダ制限設定」領域の全廃・一元化 |

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
 Mistral/      フォールバックRAG)                               Discord Gateway)
 HuggingFace/OpenRouter)
```

---

## 3. 配布パッケージ・実行環境ディレクトリ構成

ユーザーの誤操作（コンソールアプリの誤ダブルクリック）を防止し、ルート階層の視認性を高めるため、内部サブプロセスとして稼働するコンソールアプリ群は `tools/` サブフォルダに集約・隔離する。

```text
AiAssistantAvatar_Release/
├── AiAssistantAvatar.exe          (メインGUIアプリケーション)
├── AvatarSkinBuilder.exe          (スキン作成GUIツール)
├── Qt6Core.dll, Qt6Gui.dll...     (Qt6 共有DLL群)
├── libgcc_s_seh-1.dll, libstdc++  (MinGW ランタイムDLL群)
├── Config/                        (設定ファイルディレクトリ)
│   ├── local_settings.json
│   ├── blacklist.txt
│   └── whitelist.txt
├── knowledge/                     (ナレッジデータディレクトリ)
├── pic/                           (アバター画像・アセット)
└── tools/                         (内部コンソールアプリ集約フォルダ)
    ├── WebSearcher.exe            (Web検索実行CLI)
    ├── CommunityObserver.exe      (コミュニティ監視CLI)
    ├── TwitchIntroGenerator.exe   (Twitch紹介文生成CLI)
    ├── GeminiChatter.exe          (Gemini推論実行CLI)
    └── MistralChatter.exe         (Mistral推論実行CLI)
```


- [2.22 AIプロバイダ高速応答保証・短縮タイムアウト（8秒）＆ Gemini 2.5 Flash 正規化 (F-45)](AIClientManager.md#222-aiプロバイダ高速応答保証短縮タイムアウト8秒-gemini-25-flash-正規化-f-45)

- [2.23 GroqChatter および SakuraChatter 独立CLIツール (F-46)](AIClientManager.md#223-groqchatter-および-sakurachatter-独立cliツール-f-46)

- [2.24 サブプロセス・CLIツール Qt プラグイン探索自動解決 ＆ Web検索・HTTPS 通信保証 (F-47)](AIClientManager.md#224-サブプロセスcliツール-qt-プラグイン探索自動解決-web検索https-通信保証-f-47)

- [2.25 Web検索結果ノイズ除去・テキストクレンジング ＆ 10秒タイムアウト最適化 ＆ 呼び名指示優先配置 (F-48)](AIClientManager.md#225-web検索結果ノイズ除去テキストクレンジング--10秒タイムアウト最適化--呼び名指示優先配置-f-48)

- [2.26 Twitch シャウトアウト IRC コマンド自動フォールバック ＆ エラーハンドリング強化 (F-49)](AIClientManager.md#226-twitch-シャウトアウト-irc-コマンド自動フォールバック--エラーハンドリング強化-f-49)

- [2.27 Mistral Pro モデル対応 ＆ 403/404 自動降格フォールバック (F-50)](AIClientManager.md#227-mistral-pro-モデル対応--403404-自動降格フォールバック-f-50)
