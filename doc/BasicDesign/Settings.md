# 基本設計書 - 設定永続化 ＆ フォーム全廃 (Settings.md)

## 1. 概要
本モジュールは、アプリケーション設定の永続化構造 (`Config/local_settings.json`) およびUIフォーム整理の設計を定義する。

---

## 2. 主要機能 ＆ コンポーネント設計

### 2.1 AI設定タブの旧「プロバイダ制限設定」領域の全廃 (`F-16-10`)
- 「AI設定」タブ内に散在・配置されていた旧プロバイダ制限設定フォームを全廃・撤廃し、新設された「レートリミット」管理専用タブへ完全一元化する。

### 2.2 設定ファイル保存パスの一元化 ＆ 自動初期化 ＆ 単体テスト分離 (`F-5`)
- **パスの一元化**: すべてのモジュール (`AvatarWindow`, `TwitchReader`, `DiscordReader`, `AIClientManager`, `main.cpp`) で設定ファイルの読み書き参照パスを `Config/local_settings.json` に完全固定・一元化する。ルート直下や作業ディレクトリ等の古いフォールバック探索を排除する。
- **初回自動生成**: 起動時または保存時に `Config/local_settings.json` が存在しない場合、同梱されている `Config/local_settings.json.sample` から自動的に `Config/local_settings.json` を複製生成して初期化する。
- **単体テスト環境の分離**: `AiAssistantAvatarTest.exe` などの単体テスト実行時は、実環境の `Config/local_settings.json` を上書き汚染しないよう、独立したテスト専用パス（`test_local_settings.json` 等）または一時ディレクトリで実行・検証する構造とする。

### 2.3 音声入力（STT）無音タイムアウト設定 ＆ 未存在キー自動補完 (`F-2`)
- **無音タイムアウト設定項目 (`voice_silence_timeout_ms`)**:
  - 音声入力における無音判定時間をミリ秒（ms）単位で定義する（デフォルト値: `1000`）。
  - 設定ファイル `Config/local_settings.json` 内に保存・管理され、アプリケーション起動時および設定更新時に動的にロードされる。
### 2.4 棒読みちゃん (Bouyomi-chan) HTTP連携設定 ＆ 未存在キー自動補完 (`F-33`)
- **棒読みちゃん設定項目 (`bouyomichan_enabled`, `bouyomichan_url`)**:
  - `bouyomichan_enabled`: 棒読みちゃん音声読み上げ機能の有効化フラグ（bool、デフォルト値: `false`）。
  - `bouyomichan_url`: 棒読みちゃん HTTP API アクセス用 URL（string、デフォルト値: `"http://localhost:50080/talk"`）。
- **未存在キーの自動補完 (Auto-Injection)**:
  - 既存の `Config/local_settings.json` に `"bouyomichan_url"` または `"bouyomichan_enabled"` キーが存在しない場合、アプリケーション起動時に自動検知し、コメント `# 棒読みちゃん HTTP 読み上げ機能設定` および `# 棒読みちゃん HTTP API URL (例: http://localhost:50080/talk)` 付きでデフォルト項目を自動追記・補完保存する。同PC上の固定パス運用だけでなく、別PCへの展開時にも IP アドレスを変更するだけで対応可能とする。

### 2.5 音声入力（STT）トリガー反応設定 ＆ 未存在キー自動補完 (`F-2`)
- **音声トリガー反応設定項目**:
  - `voice_name_reaction_enabled`: 音声入力待機時にアバター名（`avatar_name`）に反応するかどうかのフラグ（bool、デフォルト値: `true`）。
  - `voice_wakeword_enabled`: 音声入力待機時にウェイクワードに反応するかどうかのフラグ（bool、デフォルト値: `true`）。
  - `voice_wakeword`: 音声入力専用のウェイクワード文字列（string、デフォルト値: `"AIアシスタント"`。省略時は `twitch_wakeword` またはデフォルト値を流用）。
- **未存在キーの自動補完 (Auto-Injection)**:
  - 既存の `Config/local_settings.json` にこれらのキーが存在しない場合、アプリケーション起動時に自動検知し、コメント付きでデフォルト項目を自動追記・補完保存する。
- **動作パターンの組み合わせ**:
  - `voice_name_reaction_enabled: true` かつ `voice_wakeword_enabled: true` -> アバター名またはウェイクワードのどちらでも反応（両方有効 / OR条件）
  - `voice_name_reaction_enabled: false` かつ `voice_wakeword_enabled: true` -> ウェイクワードのみ反応（アバター名単体では無視）
  - `voice_name_reaction_enabled: true` かつ `voice_wakeword_enabled: false` -> アバター名のみ反応（ウェイクワード単体では無視）
  - `voice_name_reaction_enabled: false` かつ `voice_wakeword_enabled: false` -> 常時待機での起動なし（ブラウザ側PTTボタンでの発話のみ受付）



