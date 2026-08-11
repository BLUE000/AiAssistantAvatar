# 注意
このファイルは現在状態のみを保持する。
履歴を残してはならない。
完了済み作業を残してはならない。
常に最新状態へ置き換えること。
過去の情報が残っていたら過去の情報は削除してください。

# 作業の状態

## [フェーズ: 仕様書（詳細設計・単体/結合/システムテスト仕様書）改修完了 ＆ ユーザーチェック待ち]

### Twitch 接続時挨拶設定（`m_twitchGreetingCheckbox`）不具合に対する仕様書群の変更点
1. **`doc/DetailedDesign/UI.md` (6.2節)**:
   - `loadSettingsToUI` にて `local_settings.json` の `"twitch_greeting_enabled"`（フォールバック: `"greeting_enabled"`）から `m_twitchGreetingCheckbox` のチェック状態を復元する仕様を明記。
   - `saveSettingsFromUI` にて `m_twitchGreetingCheckbox->isChecked()` の値を `local_settings.json` の `"twitch_greeting_enabled"` キーに保存する仕様を明記。
2. **`doc/UnitTest.md` (3.10節)**:
   - `UT-GREET-05`: `AvatarWindow::loadSettingsToUI` が `"twitch_greeting_enabled": true` を正しくチェックボックスへ復元することを確認するテストケースを追加。
   - `UT-GREET-06`: `AvatarWindow::saveSettingsFromUI` が `m_twitchGreetingCheckbox` のチェック状態を `"twitch_greeting_enabled"` に正しく書き出すことを確認するテストケースを追加。
3. **`doc/IntegrationTest.md` (2.9節)**:
   - `IT-GREET-02`: `local_settings.json` からの `loadSettingsToUI()` 実行時に UI チェックボックスが ON 状態へ復元される結合テストケースを追加。
4. **`doc/SystemTest.md` (2.3節)**:
   - `ST-F10-04`: GUI 設定保存から Twitch 再接続時の自動挨拶送信動作を確認する手動システムテスト項目を追加。
   - `ST-F10-05`: GUI 設定OFF保存から Twitch 再接続時の自動挨拶抑制動作を確認する手動システムテスト項目を追加。

### これから行うこと
- Vモデルに従い、仕様書改修内容に対するユーザー様のチェックとOK（ご承認）をいただく。
- ソースコードおよび実装修正は行わず、ユーザー様のご指示を待つ。
