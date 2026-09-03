# 詳細設計書 - Discord 連携モジュール (DetailedDesign/Discord.md)

## 1. 概要
本ドキュメントは、AI Assistant Avatar における Discord 連携機能、独立 CLI ツール `tools/DiscordObserver.exe` (F-52)、Gateway WebSocket 接続、REST API メッセージ送信、およびメインアプリ（`DiscordReader`）との標準入出力 (JSON Lines) IPC 通信の詳細設計を定義する。

---

## 2. アーキテクチャ構成

```text
┌─────────────────────────────────────────────────────────────┐
│  メインアプリ (AiAssistantAvatar.exe)                        │
│                                                             │
│   DiscordReader (QProcess ラッパー / アダプタ)              │
│     ├─ QProcess::start("tools/DiscordObserver.exe --daemon") │
│     ├─ stdin への JSON Lines 書き込み (送信・リロード指示)  │
│     └─ stdout からの JSON Lines 読み取り (着信・イベント通知)│
└──────────────────────────┬───▲──────────────────────────────┘
               stdin (JSONL)│   │stdout (JSONL)
                           ▼   │
┌──────────────────────────────┴──────────────────────────────┐
│  独立CLIツール (tools/DiscordObserver.exe)                   │
│                                                             │
│   1. WebSocket Gateway 常時接続 (Heartbeat/再接続自動制御)   │
│      (wss://gateway.discord.gg/?v=10&encoding=json)         │
│   2. メッセージ着信 (MESSAGE_CREATE) ＆ WakeWord/アバター判定 │
│      ──> stdout に JSON Lines 出力                          │
│   3. stdin からのコマンド受信 ＆ 実行                        │
│      ──> Discord REST API (POST /channels/{id}/messages)    │
└─────────────────────────────────────────────────────────────┘
```

---

## 3. CLI インターフェース仕様 (`DiscordObserver.exe`)

### 3.1 実行コマンド書式
```bash
DiscordObserver.exe [OPTIONS]
```

### 3.2 コマンドライン引数一覧
| オプション | 引数型 | 必須 | 説明 |
| :--- | :---: | :---: | :--- |
| `-d, --daemon` | なし | 任意* | 常駐デーモンモードで起動。標準入出力による双方向 JSON Lines IPC 通信を行う。 |
| `-s, --send` | なし | 任意* | ワンショット送信モードで起動。指定チャンネルへメッセージを送信して終了する。 |
| `--channel <id>` | 文字列 | `--send` 時必須 | 送信先 Discord チャンネル ID。 |
| `-t, --text <msg>` | 文字列 | `--send` 時必須 | 送信メッセージ本文。 |
| `-c, --config <path>`| 文字列 | 任意 | `local_settings.json` のファイルパス。省略時は自動探索。 |
| `-k, --bot-token <tok>`| 文字列 | 任意 | Discord Bot トークン。省略時は設定ファイルから取得。 |
| `-h, --help` | なし | 任意 | コマンドラインヘルプを表示して終了。 |

> ※ `--daemon` と `--send` のいずれか一方の動作モードを指定する（デフォルトは `--daemon`）。

### 3.3 終了ステータス
- `0`: 正常終了（ワンショット送信成功、または常駐デーモン正常終了）
- `1`: API / 通信エラー、認証エラー、または Bot トークン未設定
- `2`: 引数不正（必須引数欠落、未知の引数等）

---

## 4. プロセス間通信 (IPC) JSON Lines スキーマ仕様

常駐デーモンモード（`--daemon`）において、メインアプリと `DiscordObserver.exe` は標準入出力を介して 1 行ごとの JSON オブジェクト（JSON Lines / UTF-8）で通信を行う。

### 4.1 標準出力イベント通知 (stdout: `DiscordObserver` ──> `メインアプリ`)

#### (1) メッセージ着信通知 (`event: "message"`)
監視対象チャンネルで WakeWord または アバター名にマッチしたメッセージを受信した際に通知される。
```json
{
  "event": "message",
  "channel_id": "123456789012345678",
  "username": "streamer_fan",
  "user_id": "987654321098765432",
  "text": "おすすめのゲームを教えて！",
  "raw_content": "AIアシスタント おすすめのゲームを教えて！"
}
```

#### (2) 接続準備完了通知 (`event: "ready"`)
Gateway への Identify 成功および `READY` パケット受信完了時に通知される。
```json
{
  "event": "ready",
  "bot_id": "112233445566778899",
  "username": "MyAIAssistantBot"
}
```

#### (3) 接続挨拶イベント通知 (`event: "greeting"`)
接続完了時または手動接続時、挨拶送信が有効なチャンネルに対して AI が挨拶を生成するためのイベント。
```json
{
  "event": "greeting",
  "channel_id": "123456789012345678"
}
```

#### (4) ステータス / エラー通知 (`event: "status"`)
接続状態の変化や REST 送信エラー等をメインアプリのログへ連携するための通知。
```json
{
  "event": "status",
  "level": "warning",
  "message": "Gateway disconnected. Reconnecting in 5 seconds..."
}
```

---

### 4.2 標準入力コマンド指示 (stdin: `メインアプリ` ──> `DiscordObserver`)

#### (1) メッセージ送信指示 (`action: "send"`)
AI の応答結果やシステムメッセージを指定チャンネルへ REST API で送信する。
```json
{
  "action": "send",
  "channel_id": "123456789012345678",
  "text": "おすすめのゲームは『Celeste』だよ！"
}
```

#### (2) 設定再読み込み指示 (`action: "reload"`)
設定画面で Discord 設定や監視チャンネルが変更された際に送信される。
```json
{
  "action": "reload"
}
```

#### (3) 再接続・挨拶要求 (`action: "connect"`)
`/discord connect` コマンド等による、強制再接続と挨拶スケジュールを指示する。
```json
{
  "action": "connect"
}
```

#### (4) プロセス終了指示 (`action: "stop"`)
メインアプリ終了時に安全に Gateway 接続を切断してプロセスを終了させる。
```json
{
  "action": "stop"
}
```

---

## 5. メインアプリ側 `DiscordReader` クラス設計

メインアプリの `DiscordReader` は `QProcess` を管理し、既存の外部シグナル・スロット（`notifyEvent`, `on_requestDiscordSend` 等）との完全な上位互換性を維持する。

### 5.1 ライフサイクルとフォールバック
1. **プロセス起動**:
   - `ProcessUtils::resolveExecutablePath("DiscordObserver")` でバイナリを探索。
   - 存在する場合: `QProcess` を生成し、`ProcessUtils::configureProcessEnvironment()` で環境変数を設定して `--daemon` 引数で起動。
   - 存在しない場合（テスト時など）: 従来の内部 WebSocket / QNetworkAccessManager 実装へ自動フォールバック。
2. **stdout 受信ハンドラ**:
   - `QProcess::readyReadStandardOutput` シグナルを受信し、バッファリングされた改行区切り JSON をパース。
   - `event: "message"` ──> `AppEvent (EventType::DiscordMessageReceived)` を生成し `emit notifyEvent(event)`。
   - `event: "greeting"` ──> `sendChannelGreeting(channelId)` を呼び出し、挨拶イベントを送出。
3. **コマンド送信**:
   - `on_requestDiscordSend(channelId, text)` ──> `{"action":"send", ...}\n` を `QProcess` の stdin へ書き込み。
4. **異常終了・自動復帰**:
   - `QProcess::finished` シグナルを検知し、メインアプリが稼働中であれば 3 秒後に自動再起動を試行。
