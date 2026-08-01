# 詳細設計書 - Discord 連携モジュール (DetailedDesign/Discord.md)

## 1. 概要
本ドキュメントは、AI Assistant Avatar における Discord Bot 通信 (`DiscordReader`)、Gateway WebSocket 接続、イベント受信、および返信メッセージルーティングの詳細設計を定義する。

---

## 2. Discord 通信コンポーネント (`DiscordReader`)

### 2.1 Gateway WebSocket 接続 ＆ Heartbeat
- Discord Gateway (`wss://gateway.discord.gg/?v=10&encoding=json`) へ接続。
- 指定された Heartbeat 間隔で Heartbeat 経由メッセージを保持し、切断時は自動再接続。

### 2.2 メッセージ受信 ＆ AIClientManager ルーティング
- 監視対象チャンネル ID (`m_currentDiscordChannelId`) でのユーザー発言を検知し、`EventType::CommentReceived` として `CoreModule` 経由で `AIClientManager` へ送出。
- AI 応答完了時、REST API (`POST /channels/{channel.id}/messages`) を呼び出して Discord チャット欄へ返信テキストを投稿。
