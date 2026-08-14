# トリガー
- 星座占い
- 今日の運勢
- 星座
- 占い
- 運勢
- 牡羊座
- おひつじ座
- 牡牛座
- おうし座
- 双子座
- ふたご座
- 蟹座
- かに座
- 獅子座
- しし座
- 乙女座
- おとめ座
- 天秤座
- てんびん座
- 蠍座
- さそり座
- 射手座
- いて座
- 山羊座
- やぎ座
- 水瓶座
- みずがめ座
- 魚座
- うお座


# 優先度
- 100

# 応答ルール
- ユーザーから指定された星座について、以下の【今日の確定結果】を使って占いを伝えてください。
- 運勢やラッキーアイテムを自分で勝手に変えたり捏造してはいけません。

# 今日の確定結果
- 牡羊座: 総合運=DailyTableSelect("Zodiac", "Results", "総合運", "{Date}_牡羊座"), 順位=DailyTableSelect("Zodiac", "Results", "運勢順位", "{Date}_牡羊座"), アイテム=DailyTableSelect("Zodiac", "Results", "ラッキーアイテム", "{Date}_牡羊座"), カラー=DailyTableSelect("Zodiac", "Results", "ラッキーカラー", "{Date}_牡羊座"), アドバイス=DailyTableSelect("Zodiac", "Results", "今日のアドバイス", "{Date}_牡羊座")
- 牡牛座: 総合運=DailyTableSelect("Zodiac", "Results", "総合運", "{Date}_牡牛座"), 順位=DailyTableSelect("Zodiac", "Results", "運勢順位", "{Date}_牡牛座"), アイテム=DailyTableSelect("Zodiac", "Results", "ラッキーアイテム", "{Date}_牡牛座"), カラー=DailyTableSelect("Zodiac", "Results", "ラッキーカラー", "{Date}_牡牛座"), アドバイス=DailyTableSelect("Zodiac", "Results", "今日のアドバイス", "{Date}_牡牛座")
- 双子座: 総合運=DailyTableSelect("Zodiac", "Results", "総合運", "{Date}_双子座"), 順位=DailyTableSelect("Zodiac", "Results", "運勢順位", "{Date}_双子座"), アイテム=DailyTableSelect("Zodiac", "Results", "ラッキーアイテム", "{Date}_双子座"), カラー=DailyTableSelect("Zodiac", "Results", "ラッキーカラー", "{Date}_双子座"), アドバイス=DailyTableSelect("Zodiac", "Results", "今日のアドバイス", "{Date}_双子座")
- 蟹座: 総合運=DailyTableSelect("Zodiac", "Results", "総合運", "{Date}_蟹座"), 順位=DailyTableSelect("Zodiac", "Results", "運勢順位", "{Date}_蟹座"), アイテム=DailyTableSelect("Zodiac", "Results", "ラッキーアイテム", "{Date}_蟹座"), カラー=DailyTableSelect("Zodiac", "Results", "ラッキーカラー", "{Date}_蟹座"), アドバイス=DailyTableSelect("Zodiac", "Results", "今日のアドバイス", "{Date}_蟹座")
- 獅子座: 総合運=DailyTableSelect("Zodiac", "Results", "総合運", "{Date}_獅子座"), 順位=DailyTableSelect("Zodiac", "Results", "運勢順位", "{Date}_獅子座"), アイテム=DailyTableSelect("Zodiac", "Results", "ラッキーアイテム", "{Date}_獅子座"), カラー=DailyTableSelect("Zodiac", "Results", "ラッキーカラー", "{Date}_獅子座"), アドバイス=DailyTableSelect("Zodiac", "Results", "今日のアドバイス", "{Date}_獅子座")
- 乙女座: 総合運=DailyTableSelect("Zodiac", "Results", "総合運", "{Date}_乙女座"), 順位=DailyTableSelect("Zodiac", "Results", "運勢順位", "{Date}_乙女座"), アイテム=DailyTableSelect("Zodiac", "Results", "ラッキーアイテム", "{Date}_乙女座"), カラー=DailyTableSelect("Zodiac", "Results", "ラッキーカラー", "{Date}_乙女座"), アドバイス=DailyTableSelect("Zodiac", "Results", "今日のアドバイス", "{Date}_乙女座")
- 天秤座: 総合運=DailyTableSelect("Zodiac", "Results", "総合運", "{Date}_天秤座"), 順位=DailyTableSelect("Zodiac", "Results", "運勢順位", "{Date}_天秤座"), アイテム=DailyTableSelect("Zodiac", "Results", "ラッキーアイテム", "{Date}_天秤座"), カラー=DailyTableSelect("Zodiac", "Results", "ラッキーカラー", "{Date}_天秤座"), アドバイス=DailyTableSelect("Zodiac", "Results", "今日のアドバイス", "{Date}_天秤座")
- 蠍座: 総合運=DailyTableSelect("Zodiac", "Results", "総合運", "{Date}_蠍座"), 順位=DailyTableSelect("Zodiac", "Results", "運勢順位", "{Date}_蠍座"), アイテム=DailyTableSelect("Zodiac", "Results", "ラッキーアイテム", "{Date}_蠍座"), カラー=DailyTableSelect("Zodiac", "Results", "ラッキーカラー", "{Date}_蠍座"), アドバイス=DailyTableSelect("Zodiac", "Results", "今日のアドバイス", "{Date}_蠍座")
- 射手座: 総合運=DailyTableSelect("Zodiac", "Results", "総合運", "{Date}_射手座"), 順位=DailyTableSelect("Zodiac", "Results", "運勢順位", "{Date}_射手座"), アイテム=DailyTableSelect("Zodiac", "Results", "ラッキーアイテム", "{Date}_射手座"), カラー=DailyTableSelect("Zodiac", "Results", "ラッキーカラー", "{Date}_射手座"), アドバイス=DailyTableSelect("Zodiac", "Results", "今日のアドバイス", "{Date}_射手座")
- 山羊座: 総合運=DailyTableSelect("Zodiac", "Results", "総合運", "{Date}_山羊座"), 順位=DailyTableSelect("Zodiac", "Results", "運勢順位", "{Date}_山羊座"), アイテム=DailyTableSelect("Zodiac", "Results", "ラッキーアイテム", "{Date}_山羊座"), カラー=DailyTableSelect("Zodiac", "Results", "ラッキーカラー", "{Date}_山羊座"), アドバイス=DailyTableSelect("Zodiac", "Results", "今日のアドバイス", "{Date}_山羊座")
- 水瓶座: 総合運=DailyTableSelect("Zodiac", "Results", "総合運", "{Date}_水瓶座"), 順位=DailyTableSelect("Zodiac", "Results", "運勢順位", "{Date}_水瓶座"), アイテム=DailyTableSelect("Zodiac", "Results", "ラッキーアイテム", "{Date}_水瓶座"), カラー=DailyTableSelect("Zodiac", "Results", "ラッキーカラー", "{Date}_水瓶座"), アドバイス=DailyTableSelect("Zodiac", "Results", "今日のアドバイス", "{Date}_水瓶座")
- 魚座: 総合運=DailyTableSelect("Zodiac", "Results", "総合運", "{Date}_魚座"), 順位=DailyTableSelect("Zodiac", "Results", "運勢順位", "{Date}_魚座"), アイテム=DailyTableSelect("Zodiac", "Results", "ラッキーアイテム", "{Date}_魚座"), カラー=DailyTableSelect("Zodiac", "Results", "ラッキーカラー", "{Date}_魚座"), アドバイス=DailyTableSelect("Zodiac", "Results", "今日のアドバイス", "{Date}_魚座")

# 星座データテーブル
| ID | 星座名 | よみがな | エレメント | 支配星 |
|---|---|---|---|---|
| 1 | 牡羊座 | おひつじざ | 火 | 火星 |
| 2 | 牡牛座 | おうしざ | 地 | 金星 |
| 3 | 双子座 | ふたござ | 風 | 水星 |
| 4 | 蟹座 | かにざ | 水 | 月 |
| 5 | 獅子座 | ししざ | 火 | 太陽 |
| 6 | 乙女座 | おとめざ | 地 | 水星 |
| 7 | 天秤座 | てんびんざ | 風 | 金星 |
| 8 | 蠍座 | さそりざ | 水 | 冥王星 |
| 9 | 射手座 | いてざ | 火 | 木星 |
| 10 | 山羊座 | やぎざ | 地 | 土星 |
| 11 | 水瓶座 | みずがめざ | 風 | 天王星 |
| 12 | 魚座 | うおざ | 水 | 海王星 |
