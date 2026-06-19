# 結合試験仕様書 (Integration Test Specification)

## 1. 概要
本仕様書は、基本設計書（`BasicDesign.md`）に定義されたモジュール間の連携、スレッド間通信、非同期イベントモデル、および各種エンジンの抽象化が正しく機能しているかを検証するための試験項目を定義する。

---

## 2. 試験項目一覧

### 2.1 スレッド・ライフサイクルおよび制御フロー

| 試験ID | 対象構造 | 試験内容 | 期待される結果 | 実施方法 |
| :--- | :--- | :--- | :--- | :--- |
| **IT-THR-01** | スレッド起動 | アプリケーションの初期化処理を走らせる。 | `CoreThread`, `TwitchThread`, `STTThread`, `AIThread` が生成され、すべて `QThread::isRunning() == true` となること。 | 自動/ログ |
| **IT-THR-02** | スレッド安全終了 | アプリケーション終了処理（`CoreModule` のデストラクト/終了要求）を走らせる。 | 各スレッドに終了要求が伝達され、`QThread::wait()` を経て安全かつ完全に全スレッドがJOINして破棄されること。 | 自動/ログ |

---

### 2.2 非同期イベント連携 (Signal/Slot)

| 試験ID | 対象連携経路 | 試験内容 | 期待される結果 | 実施方法 |
| :--- | :--- | :--- | :--- | :--- |
| **IT-EVT-01** | Twitch → Core | `TwitchReader` (Twitchスレッド) から `notifyEvent()` シグナルを発火する。 | コアスレッドで動作する `CoreModule::on_notify_events()` スロットが呼び出され、受信データが正しく処理されること。 | 自動テスト |
| **IT-EVT-02** | STT → Core | `STTManager` (STTスレッド) から `notifyEvent()` シグナルを発火する。 | `CoreModule::on_notify_events()` がスレッドを越えて呼び出され、音声認識結果テキストが受信されること。 | 自動テスト |
| **IT-EVT-03** | Core → UI | `CoreModule` から `notifyEventToUI()` シグナルを発火する。 | メイン（GUI）スレッドで動作する `AvatarWindow::on_notify_events()` が呼び出され、イベントの種類に応じた描画更新処理がキックされること。 | 自動テスト |
| **IT-EVT-04** | Twitch認証連携 | `TwitchReader` 起動時にトークンがない場合に仮HTTPサーバーを起動し、取得したトークンでIRC接続する。 | ローカルHTTPサーバーが指定ポートで待機し、ブラウザリダイレクトからトークンを取得・保存して、自動接続フローへ移行すること。 | 結合テスト |
| **IT-EVT-05** | 会話履歴取得連携 | UIから `requestChatHistory` シグナルを発火し、コア経由で `AIClientManager` の `on_requestChatHistory` スロットを呼び出す。 | 履歴データがマークダウン形式で `AppEvent` に包まれてUIに到達し、QTextBrowserに表示されること。 | 自動テスト / 結合テスト |

---

### 2.3 エンジンの動的切り替えおよび抽象化

| 試験ID | 対象モジュール | 試験内容 | 期待される結果 | 実施方法 |
| :--- | :--- | :--- | :--- | :--- |
| **IT-ABS-01** | STTエンジン切り替え | `STTManager::setEngine()` を呼び出し、"whisper" と "sapi" を交互に切り替える。 | `STTManager` 内部の `ISTTEngine` 具象クラス（`WhisperEngine` ⇔ `SAPIEngine`）が正しく切り替わり、それぞれの初期化・監視関数が呼ばれること. | 自動テスト |
| **IT-ABS-02** | AIクライアント切り替え | `AIClientManager::setAIProvider()` を呼び出し、"mistral" と "dummy" を切り替える。 | `AIClientManager` 内部の `IAIClient` 具象クラス（`MistralAIClient` ⇔ `DummyAIClient`）が正しく切り替わり、それぞれの要求メソッドに委譲されること。 | 自動テスト |

---

### 2.4 レイアウトおよび描画補正

| 試験ID | 対象機能 | 試験内容 | 期待される結果 | 実施方法 |
| :--- | :--- | :--- | :--- | :--- |
| **IT-LAY-01** | アンカー位置補正 | アバターの状態を `idle` (anchorX: 120, anchorY: 180) から `thinking` (anchorX: 120, anchorY: 182) へ切り替える。 | デスクトップ上の目標位置を保つため、アンカー差分を考慮した座標に `AvatarWindow` の位置が正しく移動（`move()`）すること。 | 自動テスト |

---

### 2.5 セキュリティとライセンス連携

| 試験ID | 対象機能 | 試験内容 | 期待される結果 | 実施方法 |
| :--- | :--- | :--- | :--- | :--- |
| **IT-SEC-01** | 出自証明とウォーターマーク連携 | 起動時（`main.cpp`）から検証（`verifyToken()`）、結果取得、UI適用（`applyWatermark()`）までの流れを実行。 | 検証結果がUIに正しく伝達され、改ざん検知（`Watermarked`）時にバイナリの `BinMarkManager` 署名データに基づいた表示がタイトル・ステータスバーに反映されること。 | 結合テスト |
| **IT-SEC-02** | セッションリセット連携 | UIから `resetSessionRequested` シグナルを発火し、コア経由で `AIClientManager` のリセットロジックと暗号化バックアップを実行する。 | 1. `AIClientManager::resetSession` が手動（`isManual == true`）で実行されること。<br>2. TransCipher 経由で暗号化ログファイルが生成されること。<br>3. UIバルーンに「会話履歴をリセットしました。」が通知されること。 | 結合テスト |
| **IT-SEC-03** | 会話履歴インポート連携 | UIから `importSessionRequested` シグナルを発火し、コア経由で `AIClientManager::importSessionBackup` を呼び出す。 | 1. ファイルが復号され会話履歴が復元されること。<br>2. UIバルーンに「会話履歴をインポートしました。」と結果が表示されること。 | 結合テスト |
| **IT-SEC-04** | 会話履歴エクスポート連携 | UIから `exportSessionRequested` シグナルを発火し、コア経由で `AIClientManager::exportSessionBackup` を呼び出す。 | 1. 暗号ファイルが復号され、人間が読みやすい平文 `.txt` ファイルとしてエクスポートされること。<br>2. UIバルーンにエクスポート完了メッセージが表示されること。 | 結合テスト |


---

## 3. テストの実施方法（結合レベル）
- スレッドの起動・終了、および非同期イベント連携については、スタブモジュールを結合した自動テストプログラムを作成し、スレッド間のシグナルが `Qt::QueuedConnection` 経由で安全に送受信されるかをアサート（Assert）する。
- 描画位置補正ロジックは、モック画像データを用いて `move()` が呼び出された座標値を検証する単体・結合自動テストを実行する。
