# 注意
このファイルは現在状態のみを保持する。
履歴を残してはならない。
完了済み作業を残してはならない。
常に最新状態へ置き換えること。
過去の情報が残っていたら過去の情報は削除してください。

# 作業の状態

## [フェーズ: 仕様書・テスト仕様書改修完了（ユーザーチェック待ち）]

### 実施した修正作業と検証結果
1. **設定ファイルパス一元化 (`Config/local_settings.json`) ＆ 自動複製 ＆ 再ロード仕様の策定**:
   - `doc/Requirements.md`: `Config/local_settings.json` への完全一元化、初回起動時の `Config/local_settings.json.sample` からの自動複製、Twitch 認証開始時の `Config/local_settings.json` 同期再読み込み仕様を追加。
   - `doc/BasicDesign/Settings.md`: 全モジュールにおける一元参照設計および単体テスト環境分離設計を追加。
   - `doc/DetailedDesign/Twitch.md`: `TwitchReader::on_twitchReauthRequested()` 呼出直前に `loadSettings()` を同期的呼び出して `Config/local_settings.json` から `m_clientId` を更新する詳細シーケンスを追加。
   - `doc/DetailedDesign/UI.md`: `AvatarWindow::saveSettingsFromUI()` および `loadSettingsToUI()` の対象パス一元化を更新。
   - `doc/UnitTest.md`: テスト専用分離パス明記および `UT-TWITCH-REAUTH-01` テストケースを追加。
   - `doc/SystemTest.md`: `ST-F5-04`, `ST-F5-05` の `Config/local_settings.json` 読込明記および `ST-F5-06`（初回自動複製確認）を追加。

### これから行うこと
- 仕様書改修内容のユーザー様確認完了後、ソースコードおよびテストコードの改修・単体テスト実行へ進む。

