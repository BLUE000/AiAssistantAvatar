#include "core_module.h"
#include "utils/config_utils.h"
#include "utils/json_comment_remover.h"
#include <QDebug>
#include <QTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

CoreModule::CoreModule(QObject *parent) : QObject(parent) {
    qDebug() << "CoreModule initialized.";
    m_commentTimer = new QTimer(this);
    connect(m_commentTimer, &QTimer::timeout, this, &CoreModule::processCommentQueue);

    m_voiceSilenceTimer = new QTimer(this);
    m_voiceSilenceTimer->setSingleShot(true);
    connect(m_voiceSilenceTimer, &QTimer::timeout, this, &CoreModule::onVoiceSilenceTimeout);

    loadVoiceSettings();
}

CoreModule::~CoreModule() {
    qDebug() << "CoreModule destroyed.";
}

void CoreModule::on_notify_events(const AppEvent &event) {
    qDebug() << "CoreModule received event from" << event.source << "Type:" << static_cast<int>(event.type);
    
    switch (event.type) {
        case EventType::TwitchCommentReceived: {
            // TwitchからのメッセージはUIに中継（UIのキャーで順次処理）
            emit notifyEventToUI(event);
            break;
        }
        case EventType::DiscordMessageReceived: {
            // DiscordからのメッセージはUIに中継せず、直接AIにリクエストする (アバター非連動)
            QString channelId = event.extraData.value("channel_id").toString();
            QString username = event.extraData.value("username").toString();
            // userパラメータに [Discord:channelId] username 形式で埋め込む
            QString encodedUser = QString("[Discord:%1] %2").arg(channelId, username);
            
            qDebug() << "CoreModule: Routing Discord message to AI. User:" << encodedUser;
            emit requestAI(event.text, encodedUser, "Discord");
            break;
        }
        case EventType::AIResponseReceived:
        case EventType::DirectInputSubmitted: {
            // AI応答受信時およびダイレクト入力時、送信元プラットフォームへ500文字分割＆スローモード遅延キュー経由で返信
            if (event.source == "Discord" && event.extraData.contains("channel_id")) {
                QString channelId = event.extraData.value("channel_id").toString();
                qDebug() << "CoreModule: Queueing response back to Discord. Channel:" << channelId;
                enqueueCommentSend(CommentQueueItem::Discord, channelId, event.text);
            } else if (event.source == "Twitch" && event.extraData.contains("twitch_channel")) {
                QString twitchChannel = event.extraData.value("twitch_channel").toString();
                qDebug() << "CoreModule: Queueing response back to Twitch. Channel:" << twitchChannel;
                enqueueCommentSend(CommentQueueItem::Twitch, twitchChannel, event.text);
            }
            // Discord/Twitch/直接入力いずれの場合もUIに中継
            emit notifyEventToUI(event);
            break;
        }

        case EventType::TwitchConnectRequested: {
            // /twitch connect コマンド → TwitchReaderへ挨拶付き再接続を要求
            qDebug() << "CoreModule: Routing TwitchConnectRequested to TwitchReader.";
            emit requestTwitchConnect();
            // UIにもコマンド結果を表示
            emit notifyEventToUI(event);
            break;
        }
        case EventType::DiscordConnectRequested: {
            // /discord connect コマンド → DiscordReaderへ挨拶付き再接続を要求
            qDebug() << "CoreModule: Routing DiscordConnectRequested to DiscordReader.";
            emit requestDiscordConnect();
            // UIにもコマンド結果を表示
            emit notifyEventToUI(event);
            break;
        }
        case EventType::TwitchRaidReceived: {
            qDebug() << "CoreModule: Routing TwitchRaidReceived to AIClientManager. Raider:" << event.text;
            emit requestTwitchRaid(event.text);
            emit notifyEventToUI(event);
            break;
        }
        case EventType::ShoutoutSuccessReceived: {
            qDebug() << "CoreModule: Routing ShoutoutSuccessReceived to AIClientManager. Target:" << event.text;
            emit requestShoutoutSuccess(event.text);
            emit notifyEventToUI(event);
            break;
        }
        case EventType::VoiceInputCompleted: {
            qDebug() << "CoreModule: Voice input completed. Text:" << event.text << "Active:" << m_isVoiceActive;
            QString trimmedText = event.text.trimmed();
            if (trimmedText.isEmpty()) {
                emit notifyEventToUI(event);
                break;
            }

            bool isPtt = event.extraData.value("is_ptt").toBool();
            if (isPtt) {
                // PTT ボタン長押し時はアバター名チェック・状態遷移を行わずに無条件送信
                qDebug() << "CoreModule: PTT voice input. Routing directly to AI.";
                emit requestAI(trimmedText, "Streamer (Voice)", "UI");
                emit notifyEventToUI(event);
                break;
            }

            loadVoiceSettings();

            if (!m_isVoiceActive) {
                // 待機状態 (Idle): アバター名またはウェイクワードが含まれているか確認
                QString configPath = ConfigUtils::resolveConfigFilePath("local_settings.json");
                QString avatarName = "ぶるたろう";
                QString wakeword = "AIアシスタント";
                
                QFile file(configPath);
                if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    QByteArray data = JsonCommentRemover::stripHashComments(file.readAll());
                    file.close();
                    QJsonObject obj = QJsonDocument::fromJson(data).object();
                    if (obj.contains("avatar_name")) avatarName = obj.value("avatar_name").toString("ぶるたろう");
                    if (obj.contains("twitch_wakeword")) wakeword = obj.value("twitch_wakeword").toString("AIアシスタント");
                }

                auto toKatakana = [](const QString &src) -> QString {
                    QString res = src;
                    for (int i = 0; i < res.length(); ++i) {
                        ushort ch = res[i].unicode();
                        if (ch >= 0x3041 && ch <= 0x3096) {
                            res[i] = QChar(ch + 0x0060);
                        }
                    }
                    return res;
                };

                auto normalizeForMatch = [&toKatakana](const QString &src) -> QString {
                    QString s = toKatakana(src.toLower());
                    // 濁音・半濁音の表記ゆれ（例: プル -> ブル）
                    s.replace(QChar(0x30D7), QChar(0x30D6)); // プ -> ブ
                    s.replace(QChar(0x30D8), QChar(0x30D5)); // ペ -> ベ
                    s.replace(QChar(0x30D1), QChar(0x30D0)); // パ -> バ
                    s.replace(QChar(0x30D4), QChar(0x30D3)); // ピ -> ビ
                    s.replace(QChar(0x30D9), QChar(0x30D6)); // ポ -> ボ
                    // 名前の定番漢字・長音表記ゆれ（例: 太郎 / タロー / たろー -> タロウ）
                    s.replace("太郎", "タロウ");
                    s.replace("タロー", "タロウ");
                    s.replace("たろー", "タロウ");
                    s.replace("ロー", "ロウ");
                    // 空白・記号の除去
                    s.remove(QChar(0x3000));
                    s.remove(' ');
                    s.remove(QRegularExpression("[、。！？!?,.\\-_~〜ー]"));
                    return s;
                };

                auto stripKeywordWithHonorifics = [&toKatakana](const QString &src, const QString &keyword) -> QString {
                    if (keyword.isEmpty()) return src;

                    QString katakanaKw = toKatakana(keyword);
                    QString hiraganaKw = keyword;
                    for (int i = 0; i < hiraganaKw.length(); ++i) {
                        ushort ch = hiraganaKw[i].unicode();
                        if (ch >= 0x30A1 && ch <= 0x30F6) hiraganaKw[i] = QChar(ch - 0x0060);
                    }

                    QStringList prefixList;
                    prefixList << "ぶる" << "ブル" << "プル" << "ぶ" << "ブ" << "プ";

                    QStringList suffixList;
                    suffixList << "たろう" << "タロウ" << "太郎" << "タロー" << "たろー";

                    QSet<QString> variantSet;
                    variantSet.insert(keyword);
                    variantSet.insert(katakanaKw);
                    variantSet.insert(hiraganaKw);

                    bool containsPrefix = false;
                    for (const QString &p : prefixList) {
                        if (keyword.contains(p)) { containsPrefix = true; break; }
                    }
                    bool containsSuffix = false;
                    for (const QString &s : suffixList) {
                        if (keyword.contains(s)) { containsSuffix = true; break; }
                    }

                    if (containsPrefix && containsSuffix) {
                        for (const QString &p : prefixList) {
                            for (const QString &s : suffixList) {
                                variantSet.insert(p + s);
                            }
                        }
                    }

                    QStringList variants = variantSet.values();
                    std::sort(variants.begin(), variants.end(), [](const QString &a, const QString &b) {
                        return a.length() > b.length();
                    });

                    QStringList escapedVariants;
                    for (const QString &v : variants) {
                        escapedVariants << QRegularExpression::escape(v);
                    }

                    QString patternStr = "(?:" + escapedVariants.join("|") + ")(?:くん|君|さん|ちゃん|様|たん|殿|氏|ー|〜)*[、。！？!?\\s\\t,.]*";
                    QRegularExpression regex(patternStr, QRegularExpression::CaseInsensitiveOption);
                    QString result = src;
                    result.replace(regex, "");
                    return result.trimmed();
                };

                bool matched = false;
                QString cleanText = trimmedText;
                QString normTrimmed = normalizeForMatch(trimmedText);

                if (!wakeword.isEmpty() && normTrimmed.contains(normalizeForMatch(wakeword))) {
                    matched = true;
                    cleanText = stripKeywordWithHonorifics(trimmedText, wakeword);
                } else if (!avatarName.isEmpty() && normTrimmed.contains(normalizeForMatch(avatarName))) {
                    matched = true;
                    cleanText = stripKeywordWithHonorifics(trimmedText, avatarName);
                }

                if (matched) {
                    m_isVoiceActive = true;
                    if (m_voiceSilenceTimer) m_voiceSilenceTimer->start(m_voiceSilenceTimeoutMs);
                    if (cleanText.isEmpty()) cleanText = trimmedText;
                    qDebug() << "CoreModule: Wakeword matched. State -> Active. Routing to AI:" << cleanText;
                    emit requestAI(cleanText, "Streamer (Voice)", "UI");
                } else {
                    qDebug() << "CoreModule: No wakeword/avatar_name match while Idle. Dropping text:" << trimmedText;
                }
            } else {
                // アクティブ状態 (Active): そのままAIに送信し、タイマー再スタート
                if (m_voiceSilenceTimer) m_voiceSilenceTimer->start(m_voiceSilenceTimeoutMs);
                qDebug() << "CoreModule: Voice active. Routing to AI and resetting timer:" << trimmedText;
                emit requestAI(trimmedText, "Streamer (Voice)", "UI");
            }

            emit notifyEventToUI(event);
            break;
        }
        default:
            // その他はUIに通知中継
            emit notifyEventToUI(event);
            break;
    }
}

void CoreModule::on_startSTTRequested() {
    qDebug() << "CoreModule: STT start requested from UI.";
    emit requestSTTStart();
}

void CoreModule::on_stopSTTRequested() {
    qDebug() << "CoreModule: STT stop requested from UI.";
    emit requestSTTStop();
}


void CoreModule::on_directInputSubmitted(const QString &text) {
    qDebug() << "CoreModule: Direct text input submitted:" << text;
    
    // UIへ入力完了を即時通知
    AppEvent uiEvent;
    uiEvent.type = EventType::DirectInputSubmitted;
    uiEvent.source = "CoreModule";
    uiEvent.text = text;
    emit notifyEventToUI(uiEvent);

    // AIリクエスト要求シグナルを発火
    AppEvent sentEvent;
    sentEvent.type = EventType::AIRequestSent;
    sentEvent.source = "CoreModule";
    sentEvent.text = text;
    emit notifyEventToUI(sentEvent);

    emit requestAI(text, "", "UI");
}

void CoreModule::on_resetSessionRequested() {
    qDebug() << "CoreModule: Session reset requested from UI.";
    emit requestSessionReset(true); // 手動リセット
}

void CoreModule::on_importSessionRequested(const QString &filePath) {
    qDebug() << "CoreModule: Session import requested from UI. File:" << filePath;
    emit requestSessionImport(filePath);
}

void CoreModule::on_exportSessionRequested(const QString &encPath, const QString &txtPath) {
    qDebug() << "CoreModule: Session export requested from UI. Enc:" << encPath << "Txt:" << txtPath;
    emit requestSessionExport(encPath, txtPath);
}

void CoreModule::on_settingsUpdated() {
    qDebug() << "[TRACE-CORE] >>> CoreModule::on_settingsUpdated START";
    qDebug() << "CoreModule: Settings updated, propagating to submodules.";
    loadVoiceSettings();
    emit settingsUpdated();
    qDebug() << "[TRACE-CORE] <<< CoreModule::on_settingsUpdated END";
}

void CoreModule::on_twitchReauthRequested() {
    qDebug() << "[TRACE-CORE] >>> CoreModule::on_twitchReauthRequested START";
    qDebug() << "CoreModule: Twitch reauth requested, propagating to TwitchReader.";
    emit requestTwitchReauth();
    qDebug() << "[TRACE-CORE] <<< CoreModule::on_twitchReauthRequested END";
}

void CoreModule::on_deleteKnowledgeRequested(const QString &id) {
    qDebug() << "CoreModule: Propagation of deleteKnowledge to AIClientManager for ID:" << id;
    emit requestDeleteKnowledge(id);
}

void CoreModule::on_requestKnowledgeMetadata() {
    qDebug() << "CoreModule: Propagation of requestKnowledgeMetadata to AIClientManager";
    emit requestKnowledgeMetadata();
}

QStringList CoreModule::splitTextForComment(const QString &text, int maxLen) {
    QStringList result;
    if (text.trimmed().isEmpty()) return result;

    QString remaining = text.trimmed();
    while (remaining.length() > maxLen) {
        QString chunk = remaining.left(maxLen);
        int splitPos = -1;

        // 1. 優先境界文字（句読点・改行）を探索
        static const QString separators[] = {"。", "！", "？", "\n", "!", "?"};
        for (const QString &sep : separators) {
            int pos = chunk.lastIndexOf(sep);
            if (pos > 0 && pos > splitPos) {
                splitPos = pos + sep.length();
            }
        }

        // 2. 次点境界文字（読点・カンマ・スペース）を探索
        if (splitPos <= 0) {
            static const QString seps2[] = {"、", ",", " "};
            for (const QString &sep : seps2) {
                int pos = chunk.lastIndexOf(sep);
                if (pos > 0 && pos > splitPos) {
                    splitPos = pos + sep.length();
                }
            }
        }

        // 3. 境界が見つからない場合は maxLen で強制切断
        if (splitPos <= 0) {
            splitPos = maxLen;
        }

        QString piece = remaining.left(splitPos).trimmed();
        if (!piece.isEmpty()) {
            result.append(piece);
        }
        remaining = remaining.mid(splitPos).trimmed();
    }

    if (!remaining.isEmpty()) {
        result.append(remaining);
    }

    return result;
}

void CoreModule::enqueueCommentSend(CommentQueueItem::Target target, const QString &destination, const QString &fullText) {
    QStringList chunks = splitTextForComment(fullText, 500);
    for (const QString &chunk : chunks) {
        CommentQueueItem item;
        item.target = target;
        item.destination = destination;
        item.text = chunk;
        m_commentQueue.append(item);
    }

    if (!m_commentQueue.isEmpty() && m_commentTimer && !m_commentTimer->isActive()) {
        processCommentQueue(); // 1通目は即時送信
        if (!m_commentQueue.isEmpty()) {
            m_commentTimer->start(m_slowModeIntervalMs); // 2通目以降はスローモード対応インターバルを設ける
        }
    }
}

void CoreModule::processCommentQueue() {
    if (m_commentQueue.isEmpty()) {
        if (m_commentTimer && m_commentTimer->isActive()) {
            m_commentTimer->stop();
        }
        return;
    }

    CommentQueueItem item = m_commentQueue.takeFirst();
    if (item.target == CommentQueueItem::Discord) {
        qDebug() << "CoreModule: Sending queued comment to Discord. Dest:" << item.destination << "Len:" << item.text.length();
        emit requestDiscordSend(item.destination, item.text);
    } else if (item.target == CommentQueueItem::Twitch) {
        qDebug() << "CoreModule: Sending queued comment to Twitch. Dest:" << item.destination << "Len:" << item.text.length();
        emit requestTwitchSend(item.destination, item.text);
    }

    if (m_commentQueue.isEmpty() && m_commentTimer && m_commentTimer->isActive()) {
        m_commentTimer->stop();
    }
}

void CoreModule::onVoiceSilenceTimeout() {
    qDebug() << "CoreModule: Voice silence timeout (" << m_voiceSilenceTimeoutMs << "ms). State -> Idle.";
    m_isVoiceActive = false;
}

void CoreModule::loadVoiceSettings() {
    ensureVoiceSilenceTimeoutSettingExists();
    QString configPath = ConfigUtils::resolveConfigFilePath("local_settings.json");
    QFile file(configPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QByteArray data = JsonCommentRemover::stripHashComments(file.readAll());
        file.close();
        QJsonObject obj = QJsonDocument::fromJson(data).object();
        if (obj.contains("voice_silence_timeout_ms")) {
            m_voiceSilenceTimeoutMs = obj.value("voice_silence_timeout_ms").toInt(1000);
        }
    }
}

void CoreModule::ensureVoiceSilenceTimeoutSettingExists() {
    QString configPath = ConfigUtils::resolveConfigFilePath("local_settings.json");
    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QString content = QString::fromUtf8(file.readAll());
    file.close();

    if (!content.contains("voice_silence_timeout_ms")) {
        int lastBrace = content.lastIndexOf('}');
        if (lastBrace != -1) {
            // lastBrace より前のテキストを行単位に分割
            QString headerText = content.left(lastBrace);
            QStringList lines = headerText.split('\n');
            
            // コメント行や空行を除外した、直前の実効設定行を探す
            int targetLineIdx = -1;
            for (int i = lines.size() - 1; i >= 0; --i) {
                QString trimmed = lines[i].trimmed();
                if (!trimmed.isEmpty() && !trimmed.startsWith('#') && !trimmed.startsWith("//")) {
                    targetLineIdx = i;
                    break;
                }
            }

            // 直前の設定行の末尾にカンマがない場合は確実にカンマを補完
            if (targetLineIdx != -1) {
                QString line = lines[targetLineIdx];
                int lastCharPos = line.length() - 1;
                while (lastCharPos >= 0 && line[lastCharPos].isSpace()) {
                    lastCharPos--;
                }
                if (lastCharPos >= 0 && line[lastCharPos] != ',' && line[lastCharPos] != '{') {
                    line.insert(lastCharPos + 1, ",");
                    lines[targetLineIdx] = line;
                }
            }

            // 新項目（ファイル末尾になるため項目末尾にカンマなし）を挿入
            lines.append("  # 音声入力の無音タイムアウト時間（ミリ秒指定。指定時間を経過するとウェイクワード待機へ復帰）");
            lines.append("  \"voice_silence_timeout_ms\": 1000");

            QString updatedContent = lines.join('\n') + "\n}\n";

            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                file.write(updatedContent.toUtf8());
                file.close();
                qDebug() << "CoreModule: Auto-injected voice_silence_timeout_ms setting into" << configPath;
            }
        }
    }
}



