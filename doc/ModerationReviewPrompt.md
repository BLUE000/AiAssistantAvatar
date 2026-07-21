# AIモデル（Mistral等）向け フィルタリング設定レビュー用プロンプト

以下のテキスト全文を、Mistral AI などの LLM にそのまま貼り付けてレビュー・追加修正を依頼してください。

---

```markdown
あなたは高度なテキストフィルタリングとセーフティシステムの専門家です。
配信アバターアプリで使用している「多層スコア判定フィルタリングシステム」の設定ファイル（`blacklist.txt` および `whitelist.txt`）のレビューと追加・改善案の作成をお願いします。

### 1. システムの仕様とスコア判定ルール

本システムは以下の数式で最終危険度スコアを算出し、AIへの送信制御とマスク処理を行います：

  最終危険度スコア = (カテゴリスコア + 意図補正) - 文脈補正

【判定しきい値】
- 0 ～ 29 点 (SAFE)  : 通常通過（ゲーム話題や一般的な感情表現はホワイトリスト減算でここに入ります）
- 30 ～ 69 点 (WARN) : 一部伏字化(****)、または政治・宗教話題の場合は「中立・客観的回答プロンプト」を自動注入
- 70 点以上 (BLOCK)  : 危険リクエストとしてAI応答を即座にブロック

【スコア配分の規約（※変更厳禁）】
- 軽微な話題・文脈補正      : 10 ～ 20 点
- 中度の不適切・ゲーム補正  : 30 ～ 40 点
- 重度の不適切・教意思図    : 50 ～ 70 点
- 一発BLOCK（個人情報流出等）: 90 ～ 100 点

---

### 2. 現在の設定ファイル内容

#### 【blacklist.txt】
```text
# 書式: [単語または正規表現] , [カテゴリ] , [加算スコア]
作り方を教えて, instruction, 50
買い方を教えて, instruction, 50
購入方法, instruction, 40
入手方法, instruction, 40
密売方法, instruction, 60
調合方法, instruction, 60
爆破手順, instruction, 70
サイバー攻撃手法, instruction, 70
ハッキング方法, instruction, 60

電話番号, personal_info, 100
マイナンバー, personal_info, 100
クレジットカード番号, personal_info, 100
住所を教えて, personal_info, 100
本名を教えて, personal_info, 90

覚醒剤, drug, 40
麻薬, drug, 40
大麻, drug, 40
コカイン, drug, 40
ヘロイン, drug, 40
危険ドラッグ, drug, 40
密売, drug, 40
オーバードーズ, drug, 40

殺す, violence, 30
殺害, violence, 30
爆破, violence, 40
放火, violence, 40
テロ, violence, 50
殴打, violence, 30
拷問, violence, 50
リンチ, violence, 40

自殺, self_harm, 60
自傷, self_harm, 50
リスカ, self_harm, 50
首吊り, self_harm, 60
自殺方法, self_harm, 70

ヘイトスピーチ, hate, 70
死ね, hate, 50
消えろ, hate, 40
殺してやる, hate, 60

戦争, politics, 30
開戦, politics, 30
侵攻, politics, 30
紛争, politics, 30
軍事介入, politics, 30
ミサイル発射, politics, 30
安全保障, politics, 20
経済制裁, politics, 20
領土問題, politics, 20
内戦, politics, 30
クーデター, politics, 30
政治, politics, 15
政府, politics, 15
選挙, politics, 15
外交関係, politics, 15
国際情勢, politics, 15
どうなの, politics, 10
現状は, politics, 10
宗教, religion, 20
改宗, religion, 20
```

#### 【whitelist.txt】
```text
# 書式: [単語または正規表現] , [文脈カテゴリ] , [減算スコア]
Elin, game_context, 40
RimWorld, game_context, 40
Minecraft, game_context, 40
GTA, game_context, 40
Apex, game_context, 40
VALORANT, game_context, 40
Fortnite, game_context, 40
Cyberpunk, game_context, 40
PUBG, game_context, 40
Palworld, game_context, 40
ゲーム, game_context, 30
実況, game_context, 30
配信, game_context, 30
ボス, game_context, 30
クエスト, game_context, 30
クラフト, game_context, 30
ダンジョン, game_context, 30
プレイスタイル, game_context, 30
Kill数, game_context, 40
キルストリーク, game_context, 40

kill process, tech_context, 50
suicide burn, tech_context, 50
class, tech_context, 40
execute, tech_context, 40
terminate, tech_context, 40
abort, tech_context, 40

死ぬほど, emotion_context, 40
笑い死ぬ, emotion_context, 40
ヤバい, emotion_context, 20
ウケる, emotion_context, 20
最高, emotion_context, 20
神ゲー, emotion_context, 30

旅行, travel_context, 30
観光, travel_context, 30
行き方, travel_context, 30
パスポート, travel_context, 30
飛行機, travel_context, 30
ホテル, travel_context, 30
名物, travel_context, 30
グルメ, travel_context, 30
ディズニー, travel_context, 30

歴史, history_context, 30
世界史, history_context, 30
日本史, history_context, 30
文化, history_context, 30
教科書, history_context, 30
研究, history_context, 30
```

---

### 3. レビュー・依頼タスク

上記のルールと設定を踏まえ、以下の手順で出力してください：

1. **抜け穴・漏れのある危険単語の追加提案**：
   - 配信チャットでよく使われる隠語や伏字抜け穴表現（例: 隠語や一般的な危険指示語など）があれば、上記フォーマットに準拠した形式で追加提案してください。
2. **誤検知（一般会話の巻き添え）の予防ホワイトリスト追加**：
   - 一般的なゲーム、アニメ、プログラミング、日常会話で誤ってひっかかりそうな単語があればホワイトリストへの追加を提案してください。
3. **最終出力**：
   - スコア配分規約（10〜20点、30〜40点、50〜70点、90〜100点）を崩さず、そのままファイルに貼り付けて使える完全な `blacklist.txt` と `whitelist.txt` のテキストコードブロックを出力してください。
```
