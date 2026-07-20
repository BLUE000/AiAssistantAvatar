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
| **IT-EVT-05** | 設定更新連携 | UIから `settingsUpdated` シグナルを発火し、コア経由で `AIClientManager` および `TwitchReader` の `on_settingsUpdated` スロットを呼び出す。 | 各モジュールが `local_settings.json` から設定を再ロードし、動的に適用されること。 | 自動テスト / 結合テスト |
| **IT-EVT-06** | Twitch再認可連携 | UIから `twitchReauthRequested` シグナルを発火し、コア経由で `TwitchReader::on_reauthorizeRequested` スロットを呼び出す。 | 現在の接続が切断され、トークンがクリアされた上で、一時HTTPサーバーが起動しブラウザでOAuth認可画面が開くこと。 | 結合テスト |
| **IT-EVT-07** | WebSocketブロードキャスト | アバター切り替え時やAI応答時に、`QWebSocketServer` から接続中のWebSocketクライアントへプッシュ送信される。 | 送信されたJSONデータが、イベント種別（AvatarChanged, AIResponseReceivedなど）に応じた正しいフォーマットであること。 | 結合テスト |

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
| **IT-SEC-02** | セッションリセット連携 | UIから `resetSessionRequested` シグナルを発火し、コア経由で `AIClientManager` のリセットロジックと暗号化バックアップを実行する。 | 1. `AIClientManager::resetSession` が手動（`isManual == true`）で実行されること。<br>2. TransCipher 経由で暗号化ログファイルが生成されること。<br>3. 右ペインに「会話履歴をリセットしました。」が通知されること。 | 結合テスト |
| **IT-SEC-03** | 会話履歴インポート連携 | UIから `importSessionRequested` シグナルを発火し、コア経由で `AIClientManager::importSessionBackup` を呼び出す。 | 1. ファイルが復号され会話履歴が復元されること。<br>2. 右ペインに「会話履歴をインポートしました。」と結果が表示されること。 | 結合テスト |
| **IT-SEC-04** | 会話履歴エクスポート連携 | UIから `exportSessionRequested` シグナルを発火し、コア経由で `AIClientManager::exportSessionBackup` を呼び出す。 | 1. 暗号ファイルが復号され、人間が読みやすい平文 `.txt` ファイルとしてエクスポートされること。<br>2. 右ペインにエクスポート完了メッセージが表示されること。 | 結合テスト |

---

### 2.6 Web検索およびFunction Calling連携

| 試験ID | 対象機能 | 試験内容 | 期待される結果 | 実施方法 |
| :--- | :--- | :--- | :--- | :--- |
| **IT-SRCH-01** | `SearchManager` 連携 | `MistralAIClient` が `tool_calls` を受信した際に `SearchManager` を呼び出して検索を実行する。 | 1. `SearchManager::executeSearch` が正しいクエリで呼び出されること。<br>2. 検索結果テキストが Mistral AI API のメッセージ履歴に正しく挿入され、最終回答リクエストが送られること。 | 自動テスト / 結合テスト |
| **IT-SRCH-02** | Web検索自動フォールバック | Tavily API実行時に無効なキーや接続エラーを発生させ、検索を実行する。 | 自動的かつサイレントに `DuckDuckGoSearchProvider` が呼び出され、検索結果データがAIに渡されること。 | 自動テスト / 結合テスト |
| **IT-SRCH-03** | 設定画面からのTavilyキー適用 | 設定タブで Tavily API キーを入力し、「保存して適用」を押下する。 | 1. `local_settings.json` の `tavily_api_key` にキーが書き込まれること。<br>2. `SearchManager` が保持するAPIキーが動的に更新されること。 | 結合テスト |

---

### 2.7 翻訳コマンド連携

| 試験ID | 対象機能 | 試験内容 | 期待される結果 | 実施方法 |
| :--- | :--- | :--- | :--- | :--- |
| **IT-TRANS-01** | 翻訳コマンド連携 | `CoreModule` 経由で `"trans en こんにちは"` などのコマンドを `AIClientManager` に要求する。 | 1. 内部で翻訳リクエスト判定され、会話履歴/コンテキストを空にしてAIクライアントへ転送されること。<br>2. 回答受信時に履歴への追加やログ書き出しがスキップされ、翻訳結果のみがUIに通知されること。 | 自動テスト / 結合テスト |
| **IT-TRANS-02** | 翻訳ヘルプ回答連携 | AIに対し「翻訳コマンドの使い方を教えて」といった意図の質問を送信する。 | システムプロンプトに従い、AIが `!ai trans [言語指示子] [テキスト]` のフォーマットおよび使い方を正しく案内すること。 | 結合テスト |

---

## 3. テストの実施方法（結合レベル）
- スレッドの起動・終了、および非同期イベント連携については、スタブモジュールを結合した自動テストプログラムを作成し、スレッド間のシグナルが `Qt::QueuedConnection` 経由で安全に送受信されるかをアサート（Assert）する。
- 描画位置補正ロジックは、モック画像データを用いて `move()` が呼び出された座標値を検証する単体・結合自動テストを実行する。

---

### 2.8 AIルーティング・レートリミット制御連携 (F-16)

| 試験ID | 対象機能 | 試験内容 | 期待される結果 | 実施方法 |
| :--- | :--- | :--- | :--- | :--- |
| **IT-ROUTE-01** | `AIRouter` + `RateLimitTracker` 連携 | 全クライアント `available=true` の状態でリクエストを送信する。 | 優先度最高の Groq が選択され、`GroqAIClient::sendRequest` が呼び出されること。 | 自動テスト |
| **IT-ROUTE-02** | フォールバック (RPM制限) | Groqの `rpmRemaining=0` を設定した状態でリクエストを送信する。 | Cerebras にフォールバックし、`CerebrasAIClient::sendRequest` が呼び出されること。 | 自動テスト |
| **IT-ROUTE-03** | フォールバック (連鎖) | Groq・Cerebras両方が `available=false` の状態でリクエストを送信する。 | Mistral にフォールバックし、`MistralAIClient::sendRequest` が呼び出されること。 | 自動テスト |
| **IT-ROUTE-04** | 全クライアント枯渇時 | 全クライアントが `available=false` の状態でリクエストを送信する。 | AIへのAPI呼び出しが発生せず、「最短でX分後に使用可能になります」形式の応答テキストが `AIResponseReceived` イベントで返されること。 | 自動テスト |
| **IT-ROUTE-05** | ヘッダー更新連携 | APIレスポンスに `x-ratelimit-remaining-requests: 3` ヘッダーを含めてモックする。 | コール後に `RateLimitTracker::statusOf("groq").rpmRemaining == 3` に更新されること。 | 自動テスト |
| **IT-ROUTE-06** | 永続化ファイル連携 | 使用量を記録してアプリを再起動（別インスタンスでロード）する。 | `log/usage_stats.json` からRPD残量が正しくリストアされること。 | 自動テスト |
| **IT-ROUTE-07** | Groq APIキー設定連携 | 設定タブでGroq APIキーを入力し「保存して適用」を押下する。 | `local_settings.json` に `groq_api_key` が書き込まれ、`GroqAIClient` にキーが反映されること。 | 結合テスト |
| **IT-ROUTE-08** | 自動取得ボタン連携 | Groqの `/models` エンドポイントをモックし「自動取得」ボタンを押下する。 | UIの「コンテキストウィンドウ」「ツール呼び出し」フィールドが取得値で更新されること。 | 結合テスト |
| **IT-ROUTE-09** | 自動取得失敗時のフィードバック | 「自動取得」押下時にネットワークエラーをモックする。 | 「取得できませんでした。手動で設定してください。」旨の通知がUIに表示されること。 | 結合テスト |
| **IT-ROUTE-10** | Manager AI 設定反映 | 「マネージャにAIを使用」チェックON、プロバイダ=Groq、モデル=`llama-3.1-8b-instant` を設定して保存する。 | `local_settings.json` の `manager_ai_enabled=true`, `manager_ai_provider="groq"`, `manager_ai_model="llama-3.1-8b-instant"` が書き込まれること。 | 結合テスト |

---

### 2.9 接続時挨拶個別設定連携

| 試験ID | 対象機能 | 試験内容 | 期待される結果 | 実施方法 |
| :--- | :--- | :--- | :--- | :--- |
| **IT-GREET-01** | UIと設定ファイルの連動 | 設定タブで「Twitch接続時にチャットで挨拶する」をON、「Discord接続時にチャットで挨拶する」をOFFにして「設定を保存して適用」を押下する。 | `local_settings.json` の `twitch_greeting_enabled` が `true`、`discord_greeting_enabled` が `false` にそれぞれ書き込まれ、保存されること。 | 結合テスト |

---

### 2.10 レイド・クリエイター自動紹介機能連携 (F-22)

| 試験ID | 対象機能 | 試験内容 | 期待される結果 | 実施方法 |
| :--- | :--- | :--- | :--- | :--- |
| **IT-RAID-01** | レイド自動紹介・連携フロー | `TwitchReader` から `USERNOTICE` (`raid`) を連続で擬似発火させ、`AIClientManager` 経由でシャウトアウト処理を起動する。 | 1. 相手の Bio および配信カテゴリが取得パースされ、AI紹介文投稿が即時送信されること。<br>2. 初回レイド時は `/shoutout [ユーザー名]` が即時送信され 120 秒タイマーが起動すること。<br>3. 連続して発生した2回目のレイド時は、`/shoutout` が待機キューに追加され、GUIのキュー一覧に表示されること。<br>4. 120秒経過時にキュー内の `/shoutout` が自動で遅延送信されること。 | 自動テスト / 結合テスト |
| **IT-RAID-02** | 「レイド・紹介」設定GUI連携 | GUIの「レイド・紹介」タブから各種設定の変更・保存、および待機中キューリスト（`m_shoutoutQueueListWidget`）の描画確認を行う。 | 1. `local_settings.json` に新キー群が正しく保存されること。<br>2. 送信待ち中ユーザーおよび残り待機秒数がGUI上にリアルタイムで一覧表示されること。 | 結合テスト |
| **IT-RAID-03** | `/shoutout` 成功時フォロー呼びかけ | Twitch IRC から `NOTICE` (`msg-id=shoutout_success`) を擬似発火させ、`shoutout_follow_msg_enabled = true` の状態で検証する。 | テンプレート内の `{name}` が置換されたフォロー呼びかけコメントがチャットに自動投稿されること。 | 自動テスト / 結合テスト |



