#include "core_module.h"
#include "utils/config_utils.h"
#include "utils/json_comment_remover.h"
#include "utils/wakeword_matcher.h"
#include "stt/stt_text_normalizer.h"
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
            qDebug() << "CoreModule: Routing TwitchRaidReceived to AIClientManager. Raider:" << event.text << "Extra:" << event.extraData;
            emit requestTwitchRaid(event.text, event.extraData);
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
                // 待機状態 (Idle): 設定（アバター名有効/ウェイクワード有効）に応じて照合
                QString configPath = ConfigUtils::resolveConfigFilePath("local_settings.json");
                QString avatarName = "ぶるたろう";
                
                QFile file(configPath);
                if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    QByteArray data = JsonCommentRemover::stripHashComments(file.readAll());
                    file.close();
                    QJsonObject obj = QJsonDocument::fromJson(data).object();
                    if (obj.contains("avatar_name")) avatarName = obj.value("avatar_name").toString("ぶるたろう");
                }

                QString cleanText;
                bool matched = false;
                QString effectiveWakeword = m_voiceWakeword.trimmed();

                if (m_voiceNameReactionEnabled && m_voiceWakewordEnabled) {
                    // 両方有効 (OR条件): アバター名またはウェイクワードのいずれかで反応
                    QStringList aliases;
                    if (!effectiveWakeword.isEmpty()) aliases << effectiveWakeword;
                    matched = WakewordMatcher::matchAndStrip(trimmedText, avatarName, aliases, cleanText);
                } else if (m_voiceNameReactionEnabled && !m_voiceWakewordEnabled) {
                    // アバター名のみ有効: ウェイクワード単体は無視
                    QStringList aliases;
                    matched = WakewordMatcher::matchAndStrip(trimmedText, avatarName, aliases, cleanText);
                } else if (!m_voiceNameReactionEnabled && m_voiceWakewordEnabled) {
                    // ウェイクワードのみ有効: アバター名単体は無視
                    QStringList aliases;
                    matched = WakewordMatcher::matchAndStrip(trimmedText, effectiveWakeword, aliases, cleanText);
                } else {
                    // 両方無効: 常時待機での起動は無効（PTTでのみ発話可能）
                    matched = false;
                }

                if (matched) {
                    m_isVoiceActive = true;
                    if (m_voiceSilenceTimer) m_voiceSilenceTimer->start(m_voiceSilenceTimeoutMs);
                    if (cleanText.isEmpty()) cleanText = trimmedText;
                    qDebug() << "CoreModule: Wakeword/Name matched via WakewordMatcher. State -> Active. Routing to AI:" << cleanText;
                    emit requestAI(cleanText, "Streamer (Voice)", "UI");
                } else {
                    qDebug() << "CoreModule: No wakeword/avatar_name match while Idle (nameReaction:" << m_voiceNameReactionEnabled << ", wakewordReaction:" << m_voiceWakewordEnabled << "). Dropping text:" << trimmedText;
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
        m_voiceNameReactionEnabled = obj.value("voice_name_reaction_enabled").toBool(ConfigDefaults::VOICE_NAME_REACTION_ENABLED);
        m_voiceWakewordEnabled = obj.value("voice_wakeword_enabled").toBool(ConfigDefaults::VOICE_WAKEWORD_ENABLED);
        if (obj.contains("voice_wakeword")) {
            m_voiceWakeword = obj.value("voice_wakeword").toString(ConfigDefaults::VOICE_WAKE_WORD);
        } else if (obj.contains("twitch_wakeword")) {
            m_voiceWakeword = obj.value("twitch_wakeword").toString(ConfigDefaults::VOICE_WAKE_WORD);
        } else {
            m_voiceWakeword = ConfigDefaults::VOICE_WAKE_WORD;
        }
        qDebug() << "CoreModule: Loaded voice settings -> silenceTimeout:" << m_voiceSilenceTimeoutMs
                 << "nameReaction:" << m_voiceNameReactionEnabled
                 << "wakewordReaction:" << m_voiceWakewordEnabled
                 << "wakeword:" << m_voiceWakeword;
    }
}

void CoreModule::ensureVoiceSilenceTimeoutSettingExists() {
    QString configPath = ConfigUtils::resolveConfigFilePath("local_settings.json");
    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QString content = QString::fromUtf8(file.readAll());
    file.close();

    bool needSilenceTimeout = !content.contains("voice_silence_timeout_ms");
    bool needNameReaction = !content.contains("voice_name_reaction_enabled");
    bool needWakewordReaction = !content.contains("voice_wakeword_enabled");
    bool needWakeword = !content.contains("voice_wakeword");

    if (needSilenceTimeout || needNameReaction || needWakewordReaction || needWakeword) {
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

            // 未存在の音声設定項目を自動挿入
            if (needSilenceTimeout) {
                lines.append("  # 音声入力の無音タイムアウト時間（ミリ秒指定。指定時間を経過するとウェイクワード待機へ復帰）");
                lines.append(QString("  \"voice_silence_timeout_ms\": 1000%1").arg(
                    (needNameReaction || needWakewordReaction || needWakeword) ? "," : ""));
            }
            if (needNameReaction) {
                lines.append("  # 音声入力時にアバター名（ぶるたろう 等）に反応するかどうか (true: 反応する, false: 反応しない)");
                lines.append(QString("  \"voice_name_reaction_enabled\": true%1").arg(
                    (needWakewordReaction || needWakeword) ? "," : ""));
            }
            if (needWakewordReaction) {
                lines.append("  # 音声入力時にウェイクワード（AIアシスタント 等）に反応するかどうか (true: 反応する, false: 反応しない)");
                lines.append(QString("  \"voice_wakeword_enabled\": true%1").arg(
                    needWakeword ? "," : ""));
            }
            if (needWakeword) {
                lines.append("  # 音声入力用ウェイクワード（省略時は twitch_wakeword の設定値を自動流用）");
                lines.append("  \"voice_wakeword\": \"AIアシスタント\"");
            }

            QString updatedContent = lines.join('\n') + "\n}\n";

            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                file.write(updatedContent.toUtf8());
                file.close();
                qDebug() << "CoreModule: Auto-injected voice trigger settings into" << configPath;
            }
        }
    }
}



