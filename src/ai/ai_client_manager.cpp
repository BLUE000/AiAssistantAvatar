#include "ai_client_manager.h"
#include <QThread>
#include "ai_random_utils.h"
#include "utils/config_utils.h"
#include "utils/json_comment_remover.h"

#include "twitch_helix_client.h"
#include "mistral_ai_client.h"
#include "cerebras_ai_client.h"
#include "groq_ai_client.h"
#include "huggingface_ai_client.h"
#include "openrouter_ai_client.h"
#include "sakura_ai_client.h"
#include "dummy_ai_client.h"
#include "system_response_manager.h"
#include "../moderation/score_moderation_engine.h"
#include "../search/search_manager.h"
#include "cipher_engine.h" // TransCipher
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QDate>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QDateTime>
#include <QRandomGenerator>
#include <QCoreApplication>
#include <QDebug>
#include <QTextStream>
#include <QFileInfo>
#include <QRegularExpression>
#include <QDesktopServices>
#include <QUrl>
#include <QCoreApplication>
#include <algorithm>

AIClientManager::AIClientManager(QObject *parent)
    : QObject(parent), m_provider(ConfigDefaults::AI_PROVIDER) 
{
    m_systemResponseManager = new SystemResponseManager(this);
    m_searchManager = new SearchManager(this);
    m_helixClient = new TwitchHelixClient(this);
    m_currentResetStartTime = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    m_importTimeoutTimer = new QTimer(this);
    m_importTimeoutTimer->setSingleShot(true);
    connect(m_importTimeoutTimer, &QTimer::timeout, this, &AIClientManager::onImportTimeout);

    m_shoutoutCooldownTimer = new QTimer(this);
    m_shoutoutCooldownTimer->setSingleShot(true);
    connect(m_shoutoutCooldownTimer, &QTimer::timeout, this, &AIClientManager::processNextShoutoutInQueue);

    m_shoutoutUiTimer = new QTimer(this);
    connect(m_shoutoutUiTimer, &QTimer::timeout, this, &AIClientManager::updateShoutoutUiStatus);
    m_shoutoutUiTimer->start(1000);

    // AI クライアントの全インスタンス化
    m_mistralClient = new MistralAIClient(this);
    m_cerebrasClient = new CerebrasAIClient(this);
    m_groqClient = new GroqAIClient(this);
    m_huggingfaceClient = new HuggingFaceAIClient(this);
    m_openrouterClient = new OpenRouterAIClient(this);
    m_sakuraClient = new SakuraAIClient(this);
    m_dummyClient = new DummyAIClient(this);

    m_clientMap["mistral"] = m_mistralClient;
    m_clientMap["cerebras"] = m_cerebrasClient;
    m_clientMap["groq"] = m_groqClient;
    m_clientMap["huggingface"] = m_huggingfaceClient;
    m_clientMap["openrouter"] = m_openrouterClient;
    m_clientMap["sakura"] = m_sakuraClient;
    m_clientMap["dummy"] = m_dummyClient;

    // トラッカーへの初期登録
    m_tracker.registerClient(m_mistralClient->defaultStatus());
    m_tracker.registerClient(m_cerebrasClient->defaultStatus());
    m_tracker.registerClient(m_groqClient->defaultStatus());
    m_tracker.registerClient(m_huggingfaceClient->defaultStatus());
    m_tracker.registerClient(m_openrouterClient->defaultStatus());
    m_tracker.registerClient(m_sakuraClient->defaultStatus());
    m_tracker.registerClient(m_dummyClient->defaultStatus());

    // シグナルコネクションの設定
    for (IAIClient *client : m_clientMap.values()) {
        connect(client, &IAIClient::requestFinished, this, &AIClientManager::on_clientRequestFinished);
    }

    // 過去の使用状況統計をロード
    QDir().mkpath("log");
    m_tracker.loadFromFile("log/usage_stats.json");

    // スレッド間シグナル伝達用のメタタイプ登録
    qRegisterMetaType<ProviderStatus>("ProviderStatus");
    qRegisterMetaType<QList<ProviderStatus>>("QList<ProviderStatus>");

    loadCredentials();
    loadBlacklist();
    loadWhitelist();
    loadUserNames();
    loadSessionContext();
    loadKnowledgeMetadata();
    setAIProvider(m_provider, true); // ロードされたプロバイダを設定（forceRefresh=true）
}

AIClientManager::~AIClientManager() {
    // 親子がQObjectなので自動開放されますが、二重解放防止のためにマップから削除して明示的に処理
    m_clientMap.clear();
    m_systemResponseManager = nullptr;
}

void AIClientManager::loadSessionContext() {
    QDir().mkpath("log");
    QString path = "log/session_context.md";
    QFile file(path);
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_sessionContext = QString::fromUtf8(file.readAll());
        file.close();
        qDebug() << "AIClientManager: Loaded session context from" << path;
    } else {
        m_sessionContext.clear();
    }
}

void AIClientManager::saveSessionContext(const QString &context) {
    QDir().mkpath("log");
    QString path = "log/session_context.md";
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(context.toUtf8());
        file.close();
        m_sessionContext = context;
        qDebug() << "AIClientManager: Saved session context to" << path;
    } else {
        qWarning() << "AIClientManager: Failed to write session context to" << path;
    }
}

void AIClientManager::loadSettingsFromJsonObject(const QJsonObject &obj) {
    m_transCipherKey = obj["trans_cipher_key"].toString("DefaultCipherKey123");
    m_provider = obj["ai_provider"].toString(ConfigDefaults::AI_PROVIDER);
    m_blacklistEnabled = obj.value("blacklist_enabled").toBool(true);
    m_streamerName = obj["twitch_channel"].toString().trimmed().toLower();
    m_avatarName = obj["avatar_name"].toString("AIアシスタント").trimmed();

    // APIキーとモデル設定の読み込み
    QString mistralKey = obj["mistral_api_key"].toString();
    QString cerebrasKey = obj["cerebras_api_key"].toString();
    QString groqKey = obj["groq_api_key"].toString();
    m_tavilyApiKey = obj["tavily_api_key"].toString();
    m_taskFlowEnabled = obj.value("taskflow_enabled").toBool(true);
    m_taskFlowApiUrl = obj["taskflow_api_url"].toString("https://streamers-tool.sakura.ne.jp/TaskFlow/public/schedules.php").trimmed();

    // 「自動選択 (推奨)」はクライアント側の既定モデルに委ねるため空文字に正規化する
    // (各クライアントの setModel は空文字を無視して初期モデルを維持する)
    auto normalizeModel = [](const QString &raw) -> QString {
        if (raw.isEmpty() || raw.startsWith("\u81ea\u52d5\u9078\u629e")) return QString();
        return raw;
    };
    m_groqModel        = normalizeModel(obj["groq_model"].toString());
    m_cerebrasModel    = normalizeModel(obj["cerebras_model"].toString());
    m_mistralModel     = normalizeModel(obj["mistral_model"].toString());
    m_huggingfaceModel = normalizeModel(obj["huggingface_model"].toString().trimmed());
    m_openrouterModel  = normalizeModel(obj["openrouter_model"].toString().trimmed());
    m_sakuraModel      = normalizeModel(obj["sakura_model"].toString().trimmed());


    // F-22 レイド・自動紹介設定
    m_raidAutoShoutoutEnabled = obj.value("raid_auto_shoutout_enabled").toBool(true);
    m_shoutoutConversationEnabled = obj.value("shoutout_conversation_enabled").toBool(true);
    m_shoutoutUseCommand = obj.value("shoutout_use_command").toBool(true);
    m_shoutoutFollowMsgEnabled = obj.value("shoutout_follow_msg_enabled").toBool(true);
    m_shoutoutFollowMsgTemplate = obj.value("shoutout_follow_msg_template").toString("ぜひ {name} さんをフォローしてね！");
    m_shoutoutUseAnnounce = obj.value("shoutout_use_announce").toBool(true);
    m_shoutoutAnnounceColor = obj.value("shoutout_announce_color").toString("random");
    m_shoutoutLength = obj.value("shoutout_length").toString("standard");
    m_shoutoutTone = obj.value("shoutout_tone").toString("明るく元気な口調で！");
    m_shoutoutPrefix = obj.value("shoutout_prefix").toString("【レイド感謝】");
}

void AIClientManager::loadCredentials() {
    QString configPath = ConfigUtils::resolveConfigFilePath("local_settings.json");

    ScoreModerationEngine::instance().loadBlacklist("blacklist.txt");
    ScoreModerationEngine::instance().loadWhitelist("whitelist.txt");

    if (!QFile::exists(configPath)) {
        qWarning() << "AIClientManager: local_settings.json does not exist. Using empty settings. Tried path:" << configPath;
        return;
    }

    QFile file(configPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QByteArray data = JsonCommentRemover::stripHashComments(file.readAll());
        file.close();

        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject obj = doc.object();
            loadSettingsFromJsonObject(obj);

            QString mistralKey = obj["mistral_api_key"].toString();
            QString cerebrasKey = obj["cerebras_api_key"].toString();
            QString groqKey = obj["groq_api_key"].toString();
            QString hfKey = obj["huggingface_api_key"].toString();
            QString openrouterKey = obj["openrouter_api_key"].toString();
            QString sakuraKey = obj["sakura_api_key"].toString();

            m_twitchChannel = obj["twitch_channel"].toString().trimmed();
            m_twitchUsername = obj["twitch_username"].toString().trimmed();
            QString twitchToken = obj["twitch_oauth_token"].toString();
            QString twitchClientId = obj["twitch_client_id"].toString();
            if (m_helixClient) {
                m_helixClient->setCredentials(twitchToken, twitchClientId);
            }

            if (m_searchManager) {
                m_searchManager->setTavilyApiKey(m_tavilyApiKey);
            }

            // 各クライアントにキーとモデルを設定
            m_mistralClient->setApiKey(mistralKey);
            m_mistralClient->setModel(m_mistralModel);
            m_mistralClient->setTavilyApiKey(m_tavilyApiKey);
            {
                ProviderStatus s = m_tracker.statusOf("mistral");
                s.available = !mistralKey.trimmed().isEmpty();
                m_tracker.registerClient(s);
            }

            m_cerebrasClient->setApiKey(cerebrasKey);
            m_cerebrasClient->setModel(m_cerebrasModel);
            m_cerebrasClient->setTavilyApiKey(m_tavilyApiKey);
            {
                ProviderStatus s = m_tracker.statusOf("cerebras");
                s.available = !cerebrasKey.trimmed().isEmpty();
                m_tracker.registerClient(s);
            }

            m_groqClient->setApiKey(groqKey);
            m_groqClient->setModel(m_groqModel);
            m_groqClient->setTavilyApiKey(m_tavilyApiKey);
            {
                ProviderStatus s = m_tracker.statusOf("groq");
                s.available = !groqKey.trimmed().isEmpty();
                m_tracker.registerClient(s);
            }

            m_huggingfaceClient->setApiKey(hfKey.trimmed());
            m_huggingfaceClient->setModel(m_huggingfaceModel.trimmed());
            m_huggingfaceClient->setTavilyApiKey(m_tavilyApiKey);
            {
                ProviderStatus s = m_tracker.statusOf("huggingface");
                s.available = !hfKey.trimmed().isEmpty();
                m_tracker.registerClient(s);
            }

            m_openrouterClient->setApiKey(openrouterKey.trimmed());
            m_openrouterClient->setModel(m_openrouterModel.trimmed());
            m_openrouterClient->setTavilyApiKey(m_tavilyApiKey);
            {
                ProviderStatus s = m_tracker.statusOf("openrouter");
                s.available = !openrouterKey.trimmed().isEmpty();
                m_tracker.registerClient(s);
            }

            m_sakuraClient->setApiKey(sakuraKey.trimmed());
            m_sakuraClient->setModel(m_sakuraModel.trimmed());
            m_sakuraClient->setTavilyApiKey(m_tavilyApiKey);
            {
                ProviderStatus s = m_tracker.statusOf("sakura");
                s.available = !sakuraKey.trimmed().isEmpty();
                m_tracker.registerClient(s);
            }

            // DummyClient は基本設定を引き継ぐ
            m_dummyClient->setApiKey(mistralKey);

            // マネージャAI設定
            m_managerEnabled = obj["manager_ai_enabled"].toBool(false);
            m_managerProvider = obj["manager_ai_provider"].toString("groq");
            m_managerModel = obj["manager_ai_model"].toString("llama-3.1-8b-instant");

            // プロバイダ制限（上限）の読み込みと適用
            if (obj.contains("provider_limits") && obj["provider_limits"].isObject()) {
                QJsonObject limitsObj = obj["provider_limits"].toObject();
                for (const QString &providerId : limitsObj.keys()) {
                    QJsonObject pLim = limitsObj[providerId].toObject();
                    ProviderStatus s = m_tracker.statusOf(providerId);
                    
                    if (pLim.contains("rpm_max")) s.rpmMax = pLim["rpm_max"].toInt();
                    if (pLim.contains("rpd_max")) s.rpdMax = pLim["rpd_max"].toInt();
                    if (pLim.contains("tpm_max")) s.tpmMax = pLim["tpm_max"].toInt();
                    if (pLim.contains("tpd_max")) s.tpdMax = pLim["tpd_max"].toInt();
                    if (pLim.contains("context")) s.contextWindow = pLim["context"].toInt();
                    if (pLim.contains("tool_call")) s.toolCall = pLim["tool_call"].toBool();
                    if (pLim.contains("cost")) s.cost = pLim["cost"].toDouble();

                    // 最低安全ガード: 過去の破壊データ（"1/1回"等）の自動クレンジング
                    int defaultRpm = 30;
                    if (providerId == "huggingface" || providerId == "openrouter" || providerId == "sakura") {
                        defaultRpm = 60;
                    }
                    if (s.rpmMax < defaultRpm && s.rpmMax > 0) {
                        qWarning() << "[AIClientManager] Corrected invalid rpmMax" << s.rpmMax << "for" << providerId << "to default" << defaultRpm;
                        s.rpmMax = defaultRpm;
                    }

                    m_tracker.setMaxValues(providerId, s);
                }
            }


            qDebug() << "AIClientManager: Loaded settings from" << configPath
                     << "Blacklist enabled:" << m_blacklistEnabled
                     << "Streamer name:" << m_streamerName
                     << "Avatar name:" << m_avatarName
                     << "Manager AI enabled:" << m_managerEnabled
                     << "Manager AI Provider:" << m_managerProvider
                     << "Manager AI Model:" << m_managerModel;

            // F-33: フォールバック候補リストを構築
            buildFallbackProviderList();
        }
    }

    // RateLimitTabWidget に更新結果を通知（QueuedConnection で UI スレッドへ安全に配信）
    emit rateLimitStatusUpdated(m_tracker.allStatuses());
}

void AIClientManager::loadBlacklist() {
    m_blacklist.clear();
    if (!m_blacklistEnabled) {
        return;
    }

    QString blacklistPath = QCoreApplication::applicationDirPath() + "/Config/blacklist.txt";
    if (!QFile::exists(blacklistPath)) {
        blacklistPath = "Config/blacklist.txt";
    }
    if (!QFile::exists(blacklistPath)) {
        blacklistPath = "blacklist.txt";
    }
#ifdef PROJECT_SOURCE_DIR
    if (!QFile::exists(blacklistPath)) {
        blacklistPath = QString(PROJECT_SOURCE_DIR) + "/Config/blacklist.txt";
    }
    if (!QFile::exists(blacklistPath)) {
        blacklistPath = QString(PROJECT_SOURCE_DIR) + "/blacklist.txt";
    }
#endif
    if (!QFile::exists(blacklistPath)) {
        blacklistPath = QCoreApplication::applicationDirPath() + "/blacklist.txt";
    }
    if (!QFile::exists(blacklistPath)) {
        blacklistPath = QCoreApplication::applicationDirPath() + "/../blacklist.txt";
    }
    if (!QFile::exists(blacklistPath)) {
        blacklistPath = QCoreApplication::applicationDirPath() + "/../../blacklist.txt";
    }

    if (!QFile::exists(blacklistPath)) {
        qDebug() << "AIClientManager: blacklist.txt does not exist. No blacklist loaded.";
        return;
    }

    QFile file(blacklistPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        in.setEncoding(QStringConverter::Utf8);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith('#')) {
                continue;
            }
            m_blacklist.append(line);
        }
        file.close();

        // 文字数の長い順に降順ソートし、長いワード（フレーズ）を優先的にマスク処理
        std::sort(m_blacklist.begin(), m_blacklist.end(), [](const QString &a, const QString &b) {
            return a.length() > b.length();
        });

        qDebug() << "AIClientManager: Loaded" << m_blacklist.size() << "blacklist words from" << blacklistPath;
    } else {
        qWarning() << "AIClientManager: Failed to open blacklist file:" << blacklistPath;
    }
}

void AIClientManager::loadWhitelist() {
    m_whitelist.clear();
    if (!m_blacklistEnabled) {
        return;
    }

    QString whitelistPath = QCoreApplication::applicationDirPath() + "/Config/whitelist.txt";
    if (!QFile::exists(whitelistPath)) {
        whitelistPath = "Config/whitelist.txt";
    }
    if (!QFile::exists(whitelistPath)) {
        whitelistPath = "whitelist.txt";
    }
#ifdef PROJECT_SOURCE_DIR
    if (!QFile::exists(whitelistPath)) {
        whitelistPath = QString(PROJECT_SOURCE_DIR) + "/Config/whitelist.txt";
    }
    if (!QFile::exists(whitelistPath)) {
        whitelistPath = QString(PROJECT_SOURCE_DIR) + "/whitelist.txt";
    }
#endif
    if (!QFile::exists(whitelistPath)) {
        whitelistPath = QCoreApplication::applicationDirPath() + "/whitelist.txt";
    }
    if (!QFile::exists(whitelistPath)) {
        whitelistPath = QCoreApplication::applicationDirPath() + "/../whitelist.txt";
    }
    if (!QFile::exists(whitelistPath)) {
        whitelistPath = QCoreApplication::applicationDirPath() + "/../../whitelist.txt";
    }

    if (!QFile::exists(whitelistPath)) {
        qDebug() << "AIClientManager: whitelist.txt does not exist. No whitelist loaded.";
        return;
    }

    QFile file(whitelistPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        in.setEncoding(QStringConverter::Utf8);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith('#')) {
                continue;
            }
            m_whitelist.append(line);
        }
        file.close();

        // 文字数の長い順に降順ソートし、長いワード（フレーズ）を優先的に保護処理
        std::sort(m_whitelist.begin(), m_whitelist.end(), [](const QString &a, const QString &b) {
            return a.length() > b.length();
        });

        qDebug() << "AIClientManager: Loaded" << m_whitelist.size() << "whitelist words from" << whitelistPath;
    } else {
        qWarning() << "AIClientManager: Failed to open whitelist file:" << whitelistPath;
    }
}

void AIClientManager::loadUserNames() {
    m_userNamesObj = QJsonObject();
    QString path = QCoreApplication::applicationDirPath() + "/Config/user_names.json";
    if (!QFile::exists(path)) {
        path = "Config/user_names.json";
    }
    if (!QFile::exists(path)) {
        path = "user_names.json";
    }
#ifdef PROJECT_SOURCE_DIR
    if (!QFile::exists(path)) {
        path = QString(PROJECT_SOURCE_DIR) + "/Config/user_names.json";
    }
    if (!QFile::exists(path)) {
        path = QString(PROJECT_SOURCE_DIR) + "/user_names.json";
    }
#endif
    if (!QFile::exists(path)) {
        path = QCoreApplication::applicationDirPath() + "/user_names.json";
    }
    if (!QFile::exists(path)) {
        path = QCoreApplication::applicationDirPath() + "/../user_names.json";
    }
    if (!QFile::exists(path)) {
        path = QCoreApplication::applicationDirPath() + "/../../user_names.json";
    }

    if (!QFile::exists(path)) {
        qDebug() << "AIClientManager: user_names.json does not exist. Using empty nickname data.";
        return;
    }

    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QByteArray data = file.readAll();
        file.close();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isNull() && doc.isObject()) {
            m_userNamesObj = doc.object();
            qDebug() << "AIClientManager: Loaded user names and nicknames data from" << path;
        } else {
            qWarning() << "AIClientManager: user_names.json has invalid JSON format.";
        }
    } else {
        qWarning() << "AIClientManager: Failed to open user_names.json.";
    }
}

QString AIClientManager::applyMask(const QString &text) const {
    if (!m_blacklistEnabled || m_blacklist.isEmpty()) {
        return text;
    }

    QString filtered = text;
    QList<QString> savedOriginals;

    // 1. ホワイトリストに含まれる単語/フレーズをプレースホルダーに退避させて保護する
    for (int i = 0; i < m_whitelist.size(); ++i) {
        const QString &word = m_whitelist[i];
        if (word.isEmpty()) continue;
        int pos = 0;
        while ((pos = filtered.indexOf(word, pos, Qt::CaseInsensitive)) != -1) {
            // 大文字小文字の元の表記を維持するために、実際にマッチした部分を抽出
            QString matched = filtered.mid(pos, word.length());
            QString ph = QString("__WHITE_LIST_PLACEHOLDER_%1__").arg(savedOriginals.size());
            savedOriginals.append(matched);

            filtered.replace(pos, word.length(), ph);
            pos += ph.length();
        }
    }

    // 2. ブラックリストワードを **** （4文字）に一律マスク（置換）する
    for (const QString &word : m_blacklist) {
        if (word.isEmpty()) continue;
        int pos = 0;
        while ((pos = filtered.indexOf(word, pos, Qt::CaseInsensitive)) != -1) {
            filtered.replace(pos, word.length(), "****");
            pos += 4; // 置換後の「****」の長さ分進める
        }
    }

    // 3. 退避していたホワイトリストの元の文字列を復元する (インデックスの誤マッチを防ぐため逆順に復元)
    for (int i = savedOriginals.size() - 1; i >= 0; --i) {
        QString ph = QString("__WHITE_LIST_PLACEHOLDER_%1__").arg(i);
        filtered.replace(ph, savedOriginals[i]);
    }

    return filtered;
}

bool AIClientManager::isLanguageIndicator(const QString &lang) const {
    QString l = lang.trimmed().toLower();
    // 2-3文字のISO言語コード (en, ja, ko, zh, fr, es, de, ru, it)
    static const QRegularExpression isoCodeRegex("^[a-z]{2,3}$");
    if (isoCodeRegex.match(l).hasMatch()) {
        return true;
    }
    
    // 既知の英語表記
    static const QStringList englishNames = {
        "english", "japanese", "korean", "chinese", "french", "spanish", 
        "german", "russian", "italian", "arabic", "portuguese", "dutch", 
        "polish", "swedish", "turkish"
    };
    if (englishNames.contains(l)) {
        return true;
    }
    
    // 既知の日本語表記（「語」で終わる、または「日本語」など）
    if (l.endsWith("語") || l == "英語" || l == "日本語" || l == "中国語" || l == "韓国語" || l == "フランス語" || l == "スペイン語" || l == "ドイツ語" || l == "ロシア語" || l == "イタリア語") {
        return true;
    }
    
    return false;
}

QString AIClientManager::mapLanguage(const QString &lang) const {
    QString l = lang.trimmed().toLower();
    if (l == "ja" || l == "jp" || l == "japanese" || l == "日本語") return "Japanese";
    if (l == "en" || l == "english" || l == "英語") return "English";
    if (l == "ko" || l == "korean" || l == "韓国語" || l == "ハングル") return "Korean";
    if (l == "zh" || l == "cn" || l == "chinese" || l == "中国語" || l == "中華") return "Chinese";
    if (l == "fr" || l == "french" || l == "フランス語") return "French";
    if (l == "es" || l == "spanish" || l == "スペイン語") return "Spanish";
    if (l == "de" || l == "german" || l == "ドイツ語") return "German";
    if (l == "ru" || l == "russian" || l == "ロシア語") return "Russian";
    if (l == "it" || l == "italian" || l == "イタリア語") return "Italian";
    
    if (lang.isEmpty()) return "Japanese";
    return lang.left(1).toUpper() + lang.mid(1);
}

void AIClientManager::setAIProvider(const QString &provider, bool forceRefresh) {
    if (!forceRefresh && m_provider == provider && m_currentClient) return;

    qDebug() << "AIClientManager: Changing default/preferred Worker AI Provider to" << provider;

    m_provider = provider;
    if (m_clientMap.contains(provider)) {
        m_currentClient = m_clientMap[provider];
    } else {
        m_currentClient = m_dummyClient;
    }
}

QStringList AIClientManager::workerPriorityOrder() const {
    QStringList order;
    if (!m_provider.isEmpty()) {
        order.append(m_provider);
    }
    QStringList defaultPriority = { "groq", "cerebras", "mistral", "huggingface", "openrouter", "sakura", "dummy" };
    for (const QString &p : defaultPriority) {
        if (!order.contains(p)) {
            order.append(p);
        }
    }
    return order;
}

QStringList AIClientManager::managerPriorityOrder() const {
    QStringList order;
    if (!m_managerProvider.isEmpty()) {
        order.append(m_managerProvider);
    }
    QStringList defaultPriority = { "groq", "cerebras", "mistral", "dummy" };
    for (const QString &p : defaultPriority) {
        if (!order.contains(p)) {
            order.append(p);
        }
    }
    return order;
}

static void parseSenderAndMessage(const QString &src, const QString &msg, QString &outSender, QString &outText, const QString &avatarName) {
    outText = msg;

    if (src == "[AI]" || src.contains("AI")) {
        outSender = avatarName.isEmpty() ? "アバター" : avatarName;
        return;
    }

    if (src == "[Direct]") {
        outSender = "ダイレクト入力";
        return;
    }

    // "buchiushi] blue002: ！うらない！" のようなフォーマットの解析
    if (msg.contains("] ") && msg.contains(": ")) {
        int bracketIndex = msg.indexOf("] ");
        int colonIndex = msg.indexOf(": ", bracketIndex);
        if (bracketIndex != -1 && colonIndex > bracketIndex) {
            QString userPart = msg.mid(bracketIndex + 2, colonIndex - (bracketIndex + 2)).trimmed();
            if (!userPart.isEmpty()) {
                outSender = userPart;
                if (src.contains("Twitch")) outSender += " (Twitch)";
                else if (src.contains("Discord")) outSender += " (Discord)";
                outText = msg.mid(colonIndex + 2).trimmed();
                return;
            }
        }
    } else if (msg.contains(": ")) {
        int colonIndex = msg.indexOf(": ");
        if (colonIndex > 0 && colonIndex < 30 && !msg.startsWith("http://") && !msg.startsWith("https://")) {
            QString potentialUser = msg.left(colonIndex).trimmed();
            if (!potentialUser.contains(' ') && !potentialUser.contains('\n') && potentialUser != "Summary" && potentialUser != "Keywords") {
                outSender = potentialUser;
                outText = msg.mid(colonIndex + 2).trimmed();
                return;
            }
        }
    }

    if (src.startsWith("[User]")) {
        QString userStr = src.mid(6).trimmed();
        outSender = userStr.isEmpty() ? "ユーザー" : userStr;
        return;
    } else if (src.startsWith("[Twitch]")) {
        QString userStr = src.mid(8).trimmed();
        outSender = userStr.isEmpty() ? "Twitchユーザー" : userStr + " (Twitch)";
        return;
    } else if (src.startsWith("[Discord]")) {
        QString userStr = src.mid(9).trimmed();
        outSender = userStr.isEmpty() ? "Discordユーザー" : userStr + " (Discord)";
        return;
    }

    if (!src.isEmpty()) {
        outSender = src;
    } else {
        outSender = "ユーザー";
    }
}

QList<ConversationEntry> AIClientManager::getConversationEntries() const {
    QList<ConversationEntry> entries;

    // ディレクトリ探索パス候補
    QStringList logDirCandidates;
    logDirCandidates << "log"
                     << "tmp/log"
                     << "tmp"
                     << QCoreApplication::applicationDirPath() + "/log"
                     << QCoreApplication::applicationDirPath() + "/tmp/log"
                     << QCoreApplication::applicationDirPath() + "/tmp"
                     << QCoreApplication::applicationDirPath() + "/../log"
                     << QCoreApplication::applicationDirPath() + "/../tmp/log"
                     << QCoreApplication::applicationDirPath() + "/../../log"
                     << QCoreApplication::applicationDirPath() + "/../../tmp/log"
                     << QDir::currentPath() + "/log"
                     << QDir::currentPath() + "/tmp/log";
    logDirCandidates.removeDuplicates();

    QStringList archiveDirCandidates;
    archiveDirCandidates << "log/archive"
                         << "tmp/log/archive"
                         << QCoreApplication::applicationDirPath() + "/log/archive"
                         << QCoreApplication::applicationDirPath() + "/tmp/log/archive"
                         << QCoreApplication::applicationDirPath() + "/../log/archive"
                         << QCoreApplication::applicationDirPath() + "/../tmp/log/archive"
                         << QCoreApplication::applicationDirPath() + "/../../log/archive"
                         << QCoreApplication::applicationDirPath() + "/../../tmp/log/archive"
                         << QDir::currentPath() + "/log/archive"
                         << QDir::currentPath() + "/tmp/log/archive"
                         << "log/archive/archived_summaries"
                         << QCoreApplication::applicationDirPath() + "/log/archive/archived_summaries";
    archiveDirCandidates.removeDuplicates();

    // 試行する暗号鍵候補
    QStringList keysToTry;
    if (!m_transCipherKey.isEmpty()) keysToTry << m_transCipherKey;
    keysToTry << "AiAssistantAvatar" << "DefaultCipherKey123" << "";
    keysToTry.removeDuplicates();

    // 重複読み込み防止用のキー集合
    QSet<QString> processedKeys;

    // 1. log/ および tmp/log/ 内の暗号化セッションバックアップファイル (*.enc) の読み込み・復号
    for (const QString &dirPath : logDirCandidates) {
        QDir dir(dirPath);
        if (!dir.exists()) continue;

        QStringList encFilters;
        encFilters << "*.enc";
        QFileInfoList encFiles = dir.entryInfoList(encFilters, QDir::Files, QDir::Name);

        for (const QFileInfo &fi : encFiles) {
            QFile file(fi.absoluteFilePath());
            if (!file.open(QIODevice::ReadOnly)) continue;
            QByteArray encryptedData = file.readAll();
            file.close();

            CipherResult result;
            bool decrypted = false;
            for (const QString &key : keysToTry) {
                result = CipherEngine::decrypt(encryptedData, key);
                if (result.isSuccess()) {
                    decrypted = true;
                    break;
                }
            }
            if (!decrypted) continue;

            QJsonDocument doc = QJsonDocument::fromJson(result.data());
            if (doc.isNull()) continue;

            QString tsStr = fi.lastModified().toString("yyyy-MM-dd hh:mm");

            if (doc.isArray()) {
                QJsonArray array = doc.array();
                for (int i = 0; i < array.size(); ++i) {
                    QJsonObject item = array.at(i).toObject();
                    QString prompt = item.value("prompt").toString();
                    QString response = item.value("response").toString();

                    if (prompt.isEmpty() && response.isEmpty()) {
                        prompt = item.value("message").toString();
                    }

                    if (prompt.isEmpty()) continue;

                    QString key = tsStr + "|" + prompt;
                    if (processedKeys.contains(key)) continue;
                    processedKeys.insert(key);

                    QString parsedSender, parsedText;
                    parseSenderAndMessage("[User]", prompt, parsedSender, parsedText, m_avatarName);

                    ConversationEntry userEntry;
                    userEntry.timestamp = tsStr;
                    userEntry.sender = parsedSender;
                    userEntry.text = parsedText;
                    userEntry.isSummarized = false;
                    entries.append(userEntry);

                    if (!response.isEmpty()) {
                        ConversationEntry avatarEntry;
                        avatarEntry.timestamp = tsStr;
                        avatarEntry.sender = m_avatarName.isEmpty() ? "アバター" : m_avatarName;
                        avatarEntry.text = response;
                        avatarEntry.isSummarized = false;
                        entries.append(avatarEntry);
                    }
                }
            } else if (doc.isObject()) {
                QJsonObject obj = doc.object();
                QJsonArray chatArr = obj.value("chat_history").toArray();
                for (const QJsonValue &v : chatArr) {
                    QJsonObject chatObj = v.toObject();
                    QString msg = chatObj.value("message").toString();
                    if (msg.isEmpty()) continue;

                    QString itemTs = chatObj.value("timestamp").toString();
                    QDateTime dt = QDateTime::fromString(itemTs, Qt::ISODate);
                    QString formattedTs = dt.isValid() ? dt.toString("yyyy-MM-dd hh:mm") : tsStr;

                    QString key = formattedTs + "|" + msg;
                    if (processedKeys.contains(key)) continue;
                    processedKeys.insert(key);

                    QString src = chatObj.value("source").toString();
                    QString parsedSender, parsedText;
                    parseSenderAndMessage(src, msg, parsedSender, parsedText, m_avatarName);

                    ConversationEntry entry;
                    entry.timestamp = formattedTs;
                    entry.sender = parsedSender;
                    entry.text = parsedText;
                    entry.isSummarized = false;
                    entries.append(entry);
                }
            }
        }
    }

    // 2. log/archive/ および tmp/log/archive/ 内の要約JSONファイルおよび詳細対話JSONファイル (*.json) の読み込み
    for (const QString &dirPath : archiveDirCandidates) {
        QDir dir(dirPath);
        if (!dir.exists()) continue;

        // 要約ファイル
        QStringList sumFilters;
        sumFilters << "summary_*.json" << "meta_summary_*.json";
        QFileInfoList sumFiles = dir.entryInfoList(sumFilters, QDir::Files, QDir::Name);

        for (const QFileInfo &fi : sumFiles) {
            QFile file(fi.absoluteFilePath());
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            file.close();

            if (doc.isObject()) {
                QJsonObject obj = doc.object();
                QString summaryText = obj.value("summary").toString();
                if (summaryText.isEmpty()) {
                    summaryText = obj.value("meta_summary").toString();
                }
                if (summaryText.isEmpty()) continue;

                QString tsStr = fi.lastModified().toString("yyyy-MM-dd hh:mm");
                QString key = tsStr + "|SUMMARY|" + summaryText.left(30);
                if (processedKeys.contains(key)) continue;
                processedKeys.insert(key);

                ConversationEntry entry;
                entry.timestamp = tsStr;
                entry.sender = "システム要約";
                entry.text = summaryText;
                entry.isSummarized = true;
                entries.append(entry);
            }
        }

        // 詳細対話ファイル (detail_*.json, detail_session_*.json)
        QStringList detailFilters;
        detailFilters << "detail_*.json" << "detail_session_*.json";
        QFileInfoList detailFiles = dir.entryInfoList(detailFilters, QDir::Files, QDir::Name);

        for (const QFileInfo &fi : detailFiles) {
            QFile file(fi.absoluteFilePath());
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            file.close();

            if (doc.isObject()) {
                QJsonObject obj = doc.object();
                QJsonArray chatArr = obj.value("chat_history").toArray();
                for (const QJsonValue &v : chatArr) {
                    QJsonObject chatObj = v.toObject();
                    QString msg = chatObj.value("message").toString();
                    if (msg.isEmpty()) continue;

                    QString tsStr = chatObj.value("timestamp").toString();
                    QDateTime dt = QDateTime::fromString(tsStr, Qt::ISODate);
                    QString formattedTs = dt.isValid() ? dt.toString("yyyy-MM-dd hh:mm") : fi.lastModified().toString("yyyy-MM-dd hh:mm");

                    QString key = formattedTs + "|" + msg;
                    if (processedKeys.contains(key)) continue;
                    processedKeys.insert(key);

                    ConversationEntry entry;
                    entry.timestamp = formattedTs;
                    QString src = chatObj.value("source").toString();
                    if (src == "[User]" || src.contains("User")) {
                        entry.sender = "ユーザー";
                    } else if (src == "[AI]" || src.contains("AI")) {
                        entry.sender = m_avatarName.isEmpty() ? "アバター" : m_avatarName;
                    } else {
                        entry.sender = src;
                    }
                    entry.text = msg;
                    entry.isSummarized = false;
                    entries.append(entry);
                }
            }
        }
    }

    // 3. メモリ上にある現行セッションの要約コンテキスト (存在する場合)
    if (!m_sessionContext.isEmpty()) {
        QString tsStr = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm");
        QString key = tsStr + "|SUMMARY|" + m_sessionContext.left(30);
        if (!processedKeys.contains(key)) {
            processedKeys.insert(key);
            ConversationEntry summaryEntry;
            summaryEntry.timestamp = tsStr;
            summaryEntry.sender = "システム要約";
            summaryEntry.text = m_sessionContext;
            summaryEntry.isSummarized = true;
            entries.append(summaryEntry);
        }
    }

    // 4. メモリ上にある現行セッションの未サマリ直近チャット
    for (const auto &pair : m_chatHistory) {
        QString tsStr = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm");

        QString userKey = tsStr + "|" + pair.first;
        if (!processedKeys.contains(userKey)) {
            processedKeys.insert(userKey);
            ConversationEntry userEntry;
            userEntry.timestamp = tsStr;
            userEntry.sender = "ユーザー";
            userEntry.text = pair.first;
            userEntry.isSummarized = false;
            entries.append(userEntry);
        }

        QString avatarKey = tsStr + "|" + pair.second;
        if (!processedKeys.contains(avatarKey)) {
            processedKeys.insert(avatarKey);
            ConversationEntry avatarEntry;
            avatarEntry.timestamp = tsStr;
            avatarEntry.sender = m_avatarName.isEmpty() ? "アバター" : m_avatarName;
            avatarEntry.text = pair.second;
            avatarEntry.isSummarized = false;
            entries.append(avatarEntry);
        }
    }

    qDebug() << "AIClientManager::getConversationEntries: Loaded" << entries.size() << "total conversation entries.";
    return entries;
}

void AIClientManager::forceSummarizeHistory() {
    if (!m_chatHistory.isEmpty()) {
        qDebug() << "AIClientManager: Manual force summarization requested.";
        resetSession(true);
    }
}

void AIClientManager::on_requestAI(const QString &prompt, const QString &user) {
    // --- F-26 多層スコアフィルタリング評価 ---
    QStringList historyMsgs;
    for (const auto &pair : m_chatHistory) {
        historyMsgs.append(pair.second);
    }
    ModerationEvalResult modResult = ScoreModerationEngine::instance().evaluate(prompt, historyMsgs);
    if (modResult.action == ModerationAction::BLOCK) {
        qWarning() << "AIClientManager: Prompt blocked by ScoreModerationEngine. Total score:" << modResult.totalScore;
        AppEvent blockEvent;
        blockEvent.type = EventType::AIResponseReceived;
        blockEvent.source = "AIClientManager";
        blockEvent.text = "【安全保護フィルター】不適切または危険な要求が検出されたため、AI応答を中断しました。";
        emit notifyEvent(blockEvent);
        return;
    }

    QString processedPrompt = (modResult.action == ModerationAction::WARN) ? modResult.maskedText : prompt;

    // すでにリセット要約中、マージ処理中の場合はキューに入れる
    if (m_isResetting || m_isMergingSummaries) {
        qDebug() << "AIClientManager: Manager is busy (resetting:" << m_isResetting
                 << ", merging:" << m_isMergingSummaries
                 << "). Queueing request.";
        PendingRequest req;
        req.prompt = prompt;
        req.user = user;
        m_pendingRequests.append(req);
        return;
    }

    // Discord / Twitchユーザー情報のパース
    QString channelId;
    QString twitchChannel;
    QString cleanUser = user;
    if (user.startsWith("[Discord:")) {
        int closeBracketIdx = user.indexOf(']');
        if (closeBracketIdx != -1) {
            channelId = user.mid(9, closeBracketIdx - 9);
            cleanUser = user.mid(closeBracketIdx + 1).trimmed();
        }
    } else if (user.startsWith("[Twitch:")) {
        int closeBracketIdx = user.indexOf(']');
        if (closeBracketIdx != -1) {
            twitchChannel = user.mid(8, closeBracketIdx - 8);
            cleanUser = user.mid(closeBracketIdx + 1).trimmed();
        }
    } else if (user.startsWith("[Twitch]")) {
        cleanUser = user.mid(8).trimmed();
    }
    m_currentDiscordChannelId = channelId;
    m_currentTwitchChannel = twitchChannel;
    m_currentRequester = cleanUser.trimmed().toLower();

    bool isSystemGreeting = (cleanUser.toLower() == "__system_greeting__");

    // リクエスト元が切り替わった場合、直近の会話履歴を要約保存してクリア（サイレントリセット）
    if (!isSystemGreeting && !m_previousRequester.isEmpty() && m_currentRequester != m_previousRequester) {
        if (!m_chatHistory.isEmpty()) {
            qDebug() << "AIClientManager: Requester changed from [" << m_previousRequester
                     << "] to [" << m_currentRequester << "]. Triggering silent resetSession to archive previous conversation.";
            
            // 今回のリクエストを一旦キューに保留
            PendingRequest req;
            req.prompt = prompt;
            req.user = user;
            m_pendingRequests.append(req);

            // セッションリセット（非同期でAI要約）を走らせる
            resetSession(false);
            return;
        }
    }
    if (!isSystemGreeting) {
        m_previousRequester = m_currentRequester;
    }

    // ニックネームファイルを再ロード
    loadUserNames();

    // F-27 / F-29: マクロ式評価およびナレッジテーブルデータ自動検索
    QString trimmedPrompt = m_tableEngine.parseAndEvaluate(AIRandomUtils::parseAndEvaluate(prompt)).trimmed();
    QString tableContext = m_tableEngine.searchRelevantContext(trimmedPrompt);

    bool isDirectInput = user.isEmpty();

    // 手動シャウトアウト・紹介要求の判定 ("/shoutout xxx", "/so xxx", "!so xxx", または "〇〇さんを紹介して" 等)
    QString targetShoutoutUser;
    if (trimmedPrompt.startsWith("/shoutout ", Qt::CaseInsensitive)) {
        targetShoutoutUser = trimmedPrompt.mid(10).trimmed();
    } else if (trimmedPrompt.startsWith("/so ", Qt::CaseInsensitive)) {
        targetShoutoutUser = trimmedPrompt.mid(4).trimmed();
    } else if (trimmedPrompt.startsWith("!so ", Qt::CaseInsensitive)) {
        targetShoutoutUser = trimmedPrompt.mid(4).trimmed();
    } else if (trimmedPrompt.startsWith("!shoutout ", Qt::CaseInsensitive)) {
        targetShoutoutUser = trimmedPrompt.mid(10).trimmed();
    } else if (m_shoutoutConversationEnabled) {
        // 会話応答から「〇〇さんを紹介して」「〇〇を紹介して」「〇〇の紹介」等を抽出
        QRegularExpression soRegex("([a-zA-Z0-9_]{3,25})(?:さん|ちゃん|くん|氏)?(?:を|の)?(?:紹介|SO|so|シャウトアウト)");
        QRegularExpressionMatch soMatch = soRegex.match(trimmedPrompt);
        if (soMatch.hasMatch()) {
            targetShoutoutUser = soMatch.captured(1);
        }
    }

    if (!targetShoutoutUser.isEmpty()) {
        if (targetShoutoutUser.startsWith("@")) {
            targetShoutoutUser = targetShoutoutUser.mid(1).trimmed();
        }
        qDebug() << "AIClientManager: Manual shoutout requested for user:" << targetShoutoutUser;
        handleRaidShoutout(targetShoutoutUser);
        return;
    }

    // 1. スラッシュコマンド（半角）のネイティブ前処理
    if (isDirectInput && trimmedPrompt.startsWith("/")) {
        AppEvent responseEvent;
        responseEvent.source = "AIClientManager";
        responseEvent.type = EventType::AIResponseReceived;

        if (trimmedPrompt == "/open_folder") {
            openKnowledgeInputFolder();
            m_importTimeoutTimer->start(600000); // 10分
            m_importState = KnowledgeImportState::AwaitingFileAndExplanation;
            responseEvent.text = "ナレッジ入力フォルダを開きました。ファイルを配置し、チャットで「ファイル名」と「その説明」を入力してください。(10分以内に登録を開始しない場合、キャンセル確認を行います)";
        } else if (trimmedPrompt == "/cancel") {
            m_importTimeoutTimer->stop();
            m_importState = KnowledgeImportState::Idle;
            m_importingFileName.clear();
            m_importingFileContent.clear();
            responseEvent.text = "ナレッジの登録作業をキャンセルしました。";
        } else if (trimmedPrompt == "/twitch connect") {
            // TwitchConnectRequested イベントを発火 → CoreModule → TwitchReader::on_twitchConnectRequested()
            AppEvent connectEvent;
            connectEvent.type = EventType::TwitchConnectRequested;
            connectEvent.source = "AIClientManager";
            connectEvent.text = "";
            emit notifyEvent(connectEvent);
            responseEvent.text = "Twitchチャンネルへ接続します。";
        } else if (trimmedPrompt == "/discord connect") {
            // DiscordConnectRequested イベントを発火 → CoreModule → DiscordReader::on_discordConnectRequested()
            AppEvent connectEvent;
            connectEvent.type = EventType::DiscordConnectRequested;
            connectEvent.source = "AIClientManager";
            connectEvent.text = "";
            emit notifyEvent(connectEvent);
            responseEvent.text = "Discordチャンネルへ接続します。";
        } else {
            responseEvent.text = "無効なコマンドです。";
        }
        emit notifyEvent(responseEvent);
        return;
    }

    // システム固定応答モジュールによる判定
    QString activeModel = m_currentClient ? m_currentClient->currentModelName() : QString();
    QString staticResponse = m_systemResponseManager->processPrompt(trimmedPrompt, m_provider, m_avatarName, activeModel);
    if (!staticResponse.isEmpty()) {
        if (staticResponse.contains("レートリミット")) {
            emitCurrentStatus(); // レートリミットステータス更新を即時発火して画面を最新化！
        }
        AppEvent responseEvent;
        responseEvent.source = "AIClientManager";
        responseEvent.type = EventType::AIResponseReceived;
        responseEvent.text = staticResponse;
        emit notifyEvent(responseEvent);
        return;
    }


    // 2. タイムアウト時のキャンセル確認状態の処理
    if (isDirectInput && m_importState == KnowledgeImportState::CancelConfirmation) {
        QString lowerPrompt = trimmedPrompt.toLower();
        if (lowerPrompt == "yes" || lowerPrompt == "はい" || lowerPrompt == "キャンセル" || lowerPrompt == "キャンセルする") {
            m_importState = KnowledgeImportState::Idle;
            m_importingFileName.clear();
            m_importingFileContent.clear();
            AppEvent event;
            event.source = "AIClientManager";
            event.type = EventType::AIResponseReceived;
            event.text = "ナレッジ登録作業をキャンセルしました。";
            emit notifyEvent(event);
            return;
        } else if (lowerPrompt == "no" || lowerPrompt == "いいえ" || lowerPrompt == "キャンセルしない" || lowerPrompt == "続ける") {
            m_importState = KnowledgeImportState::AwaitingFileAndExplanation;
            m_importTimeoutTimer->start(600000);
            AppEvent event;
            event.source = "AIClientManager";
            event.type = EventType::AIResponseReceived;
            event.text = "登録作業を継続します。ファイルを配置し、ファイル名と説明を入力してください。(制限時間は10分間です)";
            emit notifyEvent(event);
            return;
        }
    }

    // 3. ファイル配置・説明待ち状態におけるファイル検知処理
    if (isDirectInput && m_importState == KnowledgeImportState::AwaitingFileAndExplanation) {
        // 正規表現で.mdファイルを検出
        QRegularExpression mdRegex("([a-zA-Z0-9_\\-\\.\\x{4e00}-\\x{9fa5}]+?\\.md)", QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch match = mdRegex.match(trimmedPrompt);
        if (match.hasMatch()) {
            QString fileName = match.captured(1);
            QString srcPath = "log/knowledge_input/" + fileName;
            if (QFile::exists(srcPath)) {
                QFile file(srcPath);
                if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    m_importTimeoutTimer->stop();
                    m_importingFileName = fileName;
                    m_importingFileContent = QString::fromUtf8(file.readAll());
                    file.close();
                    m_importState = KnowledgeImportState::QandAMode;
                    qDebug() << "AIClientManager: Found and read temporary markdown file:" << fileName;
                } else {
                    AppEvent errorEvent;
                    errorEvent.source = "AIClientManager";
                    errorEvent.type = EventType::ErrorOccurred;
                    errorEvent.text = QString("ファイル「%1」の読み込みに失敗しました。アクセス権限などを確認してください。").arg(fileName);
                    emit notifyEvent(errorEvent);
                    m_importTimeoutTimer->start(600000); // タイマー再スタート
                    return;
                }
            } else {
                AppEvent errorEvent;
                errorEvent.source = "AIClientManager";
                errorEvent.type = EventType::ErrorOccurred;
                errorEvent.text = QString("指定されたファイル「%1」が log/knowledge_input/ フォルダに見つかりません。ファイルを正しく配置したか確認してください。").arg(fileName);
                emit notifyEvent(errorEvent);
                m_importTimeoutTimer->start(600000); // タイマー再スタート
                return;
            }
        }
    }


    // 通常の対話に進むことが確定したため、ここでブラックリストのマスク処理を適用する
    QString filteredPrompt = applyMask(prompt);
    trimmedPrompt = filteredPrompt.trimmed();

    // 送信元タグ付きプロンプトの作成（履歴保存用）
    if (!m_currentDiscordChannelId.isEmpty()) {
        m_lastPromptWithTag = QString("[Discord] %1: %2").arg(cleanUser, filteredPrompt);
    } else if (!user.isEmpty()) {
        m_lastPromptWithTag = QString("[Twitch] %1: %2").arg(user, filteredPrompt);
    } else {
        m_lastPromptWithTag = QString("[Direct] %1").arg(filteredPrompt);
    }

    // 翻訳コマンドの判定 ("trans" で始まるか、または "!ai trans", "/ai trans" 等の前置記号付きか)
    QString transCheckPrompt = trimmedPrompt;
    static const QRegularExpression prefixRegex("^(?:!ai|/ai|!|/)\\s*", QRegularExpression::CaseInsensitiveOption);
    transCheckPrompt.remove(prefixRegex);
    transCheckPrompt = transCheckPrompt.trimmed();

    if (transCheckPrompt.startsWith("trans", Qt::CaseInsensitive)) {
        QString cmdArgs = transCheckPrompt.mid(5).trimmed(); // "trans" の後
        QString targetLanguage = "Japanese";
        QString textToTranslate = cmdArgs;

        // 引数の分解
        int firstSpaceIdx = cmdArgs.indexOf(QRegularExpression("\\s+"));
        if (firstSpaceIdx != -1) {
            QString possibleLang = cmdArgs.left(firstSpaceIdx).trimmed();
            QString possibleText = cmdArgs.mid(firstSpaceIdx).trimmed();
            if (isLanguageIndicator(possibleLang)) {
                targetLanguage = mapLanguage(possibleLang);
                textToTranslate = possibleText;
            }
        }

        m_isTranslationRequest = true;

        if (selectAndPrepareClient()) {
            // コアへ送信開始イベントを通知
            AppEvent event;
            event.type = EventType::AIRequestSent;
            event.source = "AIClientManager";
            event.text = filteredPrompt;
            emit notifyEvent(event);

            // 翻訳用のプロンプトを作成。
            // 翻訳以外の不要な発言やクォートを排除するため、厳密なインストラクションを含める。
            QString translationPrompt = QString("Translate the following text to %1. Output ONLY the translation without any other text, explanations, or quotes.\n\nText:\n%2")
                                            .arg(targetLanguage)
                                            .arg(textToTranslate);
            
            // 翻訳要求時は会話履歴とセッションコンテキストを空にして送信
            m_currentClient->sendRequest(translationPrompt, QList<QPair<QString, QString>>(), "");
        }
        return;
    }

    // 通常のチャット要求
    m_isTranslationRequest = false;

    // ユーザー名に対応した呼びかけ指示プロンプトの構築
    QString finalPrompt = filteredPrompt;
    QString additionalSystemPrompt;
    if (!cleanUser.isEmpty() && !isSystemGreeting) {
        QString systemInstructions;
        QString platformName = m_currentDiscordChannelId.isEmpty() ? "Twitch" : "Discord";
        QJsonObject usersMap = m_userNamesObj.value("users").toObject();
        QString cleanUserLower = cleanUser.trimmed().toLower();
        QJsonObject userData = findUserProfile(cleanUserLower);
        if (!userData.isEmpty()) {
            QString preferred = userData.value("preferred").toString().trimmed();
            QJsonArray nicknamesArray = userData.value("nicknames").toArray();
            QStringList nicknames;
            for (const QJsonValue &val : nicknamesArray) {
                if (!val.toString().trimmed().isEmpty()) {
                    nicknames.append(val.toString().trimmed());
                }
            }

            if (!preferred.isEmpty()) {
                // 優先される呼び名が指定されている場合
                systemInstructions = QString(
                    "[システム指示: このコメントの投稿者は「%1」さんです。回答の冒頭で，必ず「%1さん、」または「%1、」と呼びかけてください。他の呼び方は使わず、この呼び方で統一してください。また、もし今回のコメントで新たな呼び方の変更指示（例：「〇〇です」などの自己紹介や「〇〇と呼んで」などの指示）があれば、その指示に従い、今後の対話でそれを反映させてください。]"
                ).arg(preferred);
            } else if (!nicknames.isEmpty()) {
                // 愛称リストがある場合
                QString nicknamesStr = nicknames.join("、");
                systemInstructions = QString(
                    "[システム指示: このコメントの投稿者の%1アカウント名は「%2」です。愛称（呼び名）の候補は「%3」です。回答の冒頭で、これらの愛称候補からいずれか1つをランダムに選んで『〇〇さん、』や『〇〇ちゃん、』などと呼びかけて回答してください。また、もし今回のコメント内で「〇〇と呼んで」のような呼び方の指定・変更指示、あるいは「〇〇です」といった自己紹介があった場合は、その指示した呼び方を最優先で使用し、今後の回答でもその呼び方を使用してください。]"
                ).arg(platformName).arg(cleanUser).arg(nicknamesStr);
            } else {
                // 登録はあるが愛称リストも優先呼び名も空の場合 (アカウント名そのまま)
                systemInstructions = QString(
                    "[システム指示: このコメントの投稿者の%1アカウント名は「%2」です。冒頭で『%2さん、』と呼びかけて回答してください。もし今回のコメントで別の呼び方の指示や「〇〇です」などの自己紹介があれば、その指示した呼び方を使用してください。]"
                ).arg(platformName).arg(cleanUser);
            }
        } else {
            // 未登録ユーザーの場合：推測変換を行わずアカウント名そのまま＋さん
            systemInstructions = QString(
                "[システム指示: このコメントの投稿者の%1アカウント名は「%2」です。冒頭で『%2さん、』と呼びかけて回答してください。もし今回のコメント内で「〇〇と呼んで」などの呼び方の指定・変更指示、または「〇〇です」といった自己紹介があった場合は、その指示した呼び方を使用してください。]"
            ).arg(platformName).arg(cleanUser);
        }

        if (!systemInstructions.isEmpty()) {
            additionalSystemPrompt = systemInstructions;
        }
    }

    // レートリミット監視機能のナレッジをシステムプロンプトに常時注入（誤回答防止）
    QString rateLimitCapabilityKnowledge =
        "[システム機能知識: 本アプリには各AIプロバイダ（Groq, Mistral, Cerebras, さくらAI, Hugging Face, OpenRouter）の"
        "レートリミット（1分間/1日の利用枠・残量・解除カウントダウン）をリアルタイムに確認・更新表示する『レートリミット』タブ機能が実装されています。"
        "ユーザーからレートリミットの表示や更新について尋ねられた場合は、アプリの『レートリミット』タブからいつでも確認・更新できる旨を正しく回答してください。]";

    if (!additionalSystemPrompt.isEmpty()) {
        additionalSystemPrompt += "\n\n" + rateLimitCapabilityKnowledge;
    } else {
        additionalSystemPrompt = rateLimitCapabilityKnowledge;
    }

    if (isSystemGreeting) {

        finalPrompt = "接続時の最初の挨拶を行ってください。";
        additionalSystemPrompt = "[システム指示: これは配信接続時の自動挨拶要求です。配信を開始したばかりですので、配信に来てくれた視聴者に向けて明るく元気に最初の挨拶（例:『皆さんこんにちは！配信開始しました！』など）を行ってください。ユーザーからのチャット発言はありませんので、『（システム）チャンネルに接続しました』や『〜についてですね』といったシステム側の文字列をオウム返しにしたり、それに対して回答したりすることは絶対に避けてください。純粋な挨拶のみを出力してください。]";
    }

    m_lastPrompt = filteredPrompt;
    m_fallbackIndex = 0; // F-33: 新規リクエストなのでフォールバックインデックスをリセット

    // 4. ナレッジ登録対話中のプロンプトインジェクション
    if (isDirectInput && m_importState == KnowledgeImportState::QandAMode) {
        // ユーザー向けファイル内容の提示（userロール）
        QString importUserContext = QString("[現在、ナレッジファイル「%1」の登録対話モードです。以下は配置されたファイルの内容です。]\n"
                                            "--- ファイル内容 ---\n%2")
                                            .arg(m_importingFileName, m_importingFileContent);
        finalPrompt = importUserContext + "\n\n" + finalPrompt;

        // システム指示の分離（systemロール）
        QString importSysInstruction = "[システム指示: ユーザーから与えられたファイル内容と説明文を読み込み、ナレッジとして登録するために不足している情報や曖昧な点があれば質問してください。登録用キーワード（3〜5個）やデータ名（タイトル）についてもユーザーと合意してください。登録完了の合意が得られたら、速やかに `finalize_knowledge_import` ツールを呼び出して本登録を実行してください。]";
        if (!additionalSystemPrompt.isEmpty()) {
            additionalSystemPrompt += "\n\n" + importSysInstruction;
        } else {
            additionalSystemPrompt = importSysInstruction;
        }
    }

    // 5. 静的ナレッジ想起（RAG）の実行
    if (m_importState == KnowledgeImportState::Idle) {
        QString recalledStatic;
        KnowledgeIndexEntry bestEntry = m_tableEngine.resolveBestEntryForTrigger(filteredPrompt);
        if (bestEntry.isValid && !bestEntry.tableName.isEmpty()) {
            QString bestContext = m_tableEngine.selectRandomColumn(bestEntry.group, bestEntry.category, bestEntry.tableName, "");
            if (!bestContext.isEmpty()) {
                recalledStatic = QString("【ナレッジ「%1」 (優先度 %2) からの抽出結果】\n%3")
                                     .arg(bestEntry.title)
                                     .arg(bestEntry.priority)
                                     .arg(bestContext);
            }
        }

        if (recalledStatic.isEmpty()) {
            scanStaticKnowledge(filteredPrompt, recalledStatic);
        }

        if (!recalledStatic.isEmpty()) {
            finalPrompt = recalledStatic + "\n\n" + finalPrompt;
        }
    }

    if (!tableContext.isEmpty()) {
        if (!additionalSystemPrompt.isEmpty()) {
            additionalSystemPrompt += tableContext;
        } else {
            additionalSystemPrompt = tableContext.trimmed();
        }
    }

    // 長期記憶想起（RAG）処理の実行
    m_recalledContext.clear();
    scanMemorySummaries(filteredPrompt);
    if (!m_recalledContext.isEmpty()) {
        finalPrompt = m_recalledContext + "\n\n" + finalPrompt;
    }

    // TaskFlow スケジュール自動取得RAGの実行 (m_taskFlowEnabled が有効時、全入力ソース: Twitch, Discord, UI直接入力に対応)
    if (m_taskFlowEnabled) {
        QString lowerPrompt = filteredPrompt.toLower();
        if (lowerPrompt.contains("予定") || lowerPrompt.contains("スケジュール") || 
            lowerPrompt.contains("タスク") || lowerPrompt.contains("状況") || 
            lowerPrompt.contains("進捗") || lowerPrompt.contains("配信") ||
            lowerPrompt.contains("作業") || lowerPrompt.contains("schedule") || 
            lowerPrompt.contains("task") || lowerPrompt.contains("work") || 
            lowerPrompt.contains("stream")) {
            
            QString schedulesContext = getTaskFlowSchedulesContext();
            if (!additionalSystemPrompt.isEmpty()) {
                additionalSystemPrompt += "\n\n" + schedulesContext;
            } else {
                additionalSystemPrompt = schedulesContext;
            }
        }
    }

    // 常に現在日時(JST)、数式表記指示、および複数質問漏れなし回答指示をシステム指示に動的注入し、時間ハルシネーションとLaTeX文字化けおよび回答省略を根絶する
    {
        QDateTime now = QDateTime::currentDateTime();
        static const QStringList days = {"", "月", "火", "水", "木", "金", "土", "日"};
        int dayOfWeek = now.date().dayOfWeek();
        QString dayStr = (dayOfWeek >= 1 && dayOfWeek <= 7) ? days.at(dayOfWeek) : "";
        QString nowStr = QString("※現在の日時は %1(%2) %3 です（日本標準時/JST）。\n計算式や数式を出力する際は、\\textや\\timesなどのLaTeXコマンドを使用せず、『100 × 162.00 = 16,200円』のように通常の文字・記号でプレーンテキスト記述してください。\n※ユーザーのメッセージに複数の質問や話題が含まれている場合は、省略せずにそれぞれの質問に対して順を追って漏れなく回答してください。\n※入力プロンプト冒頭の [発言者: X | 宛先: Y] タグを精読し、誰が誰に対して発言したのか（配信者・視聴者・AI）を正確に把握してください。視聴者の指摘やコメントを、配信者自身の誤りや問題として誤認しないでください。")
                            .arg(now.toString("yyyy-MM-dd"))
                            .arg(dayStr)
                            .arg(now.toString("HH:mm:ss"));


        if (!additionalSystemPrompt.isEmpty()) {
            additionalSystemPrompt = nowStr + "\n\n" + additionalSystemPrompt;
        } else {
            additionalSystemPrompt = nowStr;
        }
    }

    // 6. 段階的タスク実行パイプライン (文章 → 複数Task化 → Task順次実行 → Validator検証 → Role分離プロンプト構築)
    QList<ExecutionTask> tasks = analyzeAndDecomposeTasks(filteredPrompt);
    if (!tasks.isEmpty()) {
        qDebug() << "[AIClientManager] Step 1: Decomposed prompt into" << tasks.size() << "execution tasks.";
        executeTaskPipeline(tasks);
        validateAndInjectGuards(tasks, filteredPrompt, additionalSystemPrompt);
        QString refContext = formatCombinedPrompt(tasks, filteredPrompt);
        if (!refContext.isEmpty()) {
            additionalSystemPrompt = refContext + "\n\n" + additionalSystemPrompt;
        }
    }

    m_lastFinalPrompt = finalPrompt;
    m_lastAdditionalSystemPrompt = additionalSystemPrompt;

    if (selectAndPrepareClient(filteredPrompt)) {
        // 稼働中 Worker AI / Manager AI のプロバイダ・モデル情報を additionalSystemPrompt へ自動注入
        // (ユーザーから「今使ってるAIは？」と聞かれた際に正確に自己回答できるようにする)
        {
            QString workerProvider = m_currentClient ? m_currentClient->clientId() : m_provider;
            QString workerModel    = m_currentClient ? m_currentClient->currentModelName() : QString();
            if (workerModel.isEmpty()) workerModel = "(自動選択)";

            QString managerProvider = m_managerEnabled ? m_managerProvider : "(無効)";
            QString managerModel    = m_managerEnabled ? m_managerModel    : "(無効)";
            if (m_managerEnabled && managerModel.isEmpty()) managerModel = "(自動選択)";

            QString aiStatusContext = QString(
                "\n\n【現在のAIシステム稼働ステータス】\n"
                "- 会話用 Worker AI: %1 (適用モデル: %2)\n"
                "- 評価・判断用 Manager AI: %3 (適用モデル: %4)\n"
                "※ ユーザーから現在使用されているAIプロバイダやモデル名について尋ねられた場合は、上記の名称を正確に回答してください。"
            ).arg(workerProvider, workerModel, managerProvider, managerModel);

            if (!additionalSystemPrompt.isEmpty()) {
                additionalSystemPrompt += aiStatusContext;
            } else {
                additionalSystemPrompt = aiStatusContext.trimmed();
            }
        }

        // コアへ送信開始イベントを通知
        AppEvent event;
        event.type = EventType::AIRequestSent;
        event.source = "AIClientManager";
        event.text = filteredPrompt;
        // AI サーバー保護・Groq 不正アクセス判定回避のための 600ms 送出遅延 (1秒未満の時差制御)
        QThread::msleep(600);

        m_currentClient->sendRequest(finalPrompt, m_chatHistory, m_sessionContext, additionalSystemPrompt);
    }
}


void AIClientManager::on_clientRequestFinished(const QString &responseText, bool success, int httpCode) {
    if (m_apiCallStartTimeMs > 0 && m_currentClient) {
        int elapsed = QDateTime::currentMSecsSinceEpoch() - m_apiCallStartTimeMs;
        m_tracker.recordLatency(m_currentClient->clientId(), elapsed);
        m_apiCallStartTimeMs = 0;
    }
    if (success && m_currentClient) {
        m_tracker.recordLocalConsumption(m_currentClient->clientId(), m_lastFinalPrompt.length(), responseText.length());
    }
    m_tracker.saveToFile("log/usage_stats.json");
    if (m_dummyClient) {
        m_dummyClient->stopTimer();
    }
    // RateLimitTabWidget 向け通知（aiThread 上から QueuedConnection 経由で UI スレッドへ安全に配信）
    emit rateLimitStatusUpdated(m_tracker.allStatuses());


    qDebug() << "AIClientManager: Client request finished. Success:" << success
             << "HttpCode:" << httpCode
             << "Resetting:" << m_isResetting
             << "Merging:" << m_isMergingSummaries
             << "Translation:" << m_isTranslationRequest;

    AppEvent event;
    event.source = "AIClientManager";

    if (m_isMergingSummaries) {
        m_isMergingSummaries = false;
        if (success) {
            QString summaryStr = responseText;
            QStringList keywordsList;

            int kwIdx = responseText.indexOf("Keywords:", 0, Qt::CaseInsensitive);
            int sumIdx = responseText.indexOf("Summary:", 0, Qt::CaseInsensitive);
            if (kwIdx != -1 && sumIdx != -1) {
                QString keywordsStr;
                if (kwIdx < sumIdx) {
                    keywordsStr = responseText.mid(kwIdx + 9, sumIdx - (kwIdx + 9)).trimmed();
                    summaryStr = responseText.mid(sumIdx + 8).trimmed();
                } else {
                    summaryStr = responseText.mid(sumIdx + 8, kwIdx - (sumIdx + 8)).trimmed();
                    keywordsStr = responseText.mid(kwIdx + 9).trimmed();
                }

                for (const QString &k : keywordsStr.split(",")) {
                    QString tk = k.trimmed();
                    if (!tk.isEmpty()) keywordsList.append(tk);
                }
            } else {
                keywordsList.append("アーカイブ");
            }

            QDir().mkpath("log/archive");
            QString metaId = QString("meta_%1").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
            QJsonObject metaObj;
            metaObj["meta_id"] = metaId;
            
            QJsonObject timeRange;
            timeRange["start"] = "unknown";
            timeRange["end"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            metaObj["time_range"] = timeRange;

            QJsonArray kwArr;
            for (const QString &kw : keywordsList) kwArr.append(kw);
            metaObj["keywords"] = kwArr;
            metaObj["meta_summary"] = summaryStr;

            QJsonArray childSessions;
            for (const QString &sid : m_mergingSessionIds) childSessions.append(sid);
            metaObj["child_sessions"] = childSessions;

            QString metaPath = QString("log/archive/meta_summary_%1.json").arg(metaId);
            QFile metaFile(metaPath);
            if (metaFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QJsonDocument doc(metaObj);
                metaFile.write(doc.toJson());
                metaFile.close();
                qDebug() << "AIClientManager: Saved meta-summary to" << metaPath;
            }

            QDir().mkpath("log/archive/archived_summaries");
            for (const QString &sid : m_mergingSessionIds) {
                QString src = QString("log/archive/summary_%1.json").arg(sid);
                QString dst = QString("log/archive/archived_summaries/summary_%1.json").arg(sid);
                if (QFile::exists(src)) {
                    if (QFile::exists(dst)) QFile::remove(dst);
                    if (QFile::rename(src, dst)) {
                        qDebug() << "AIClientManager: Archived summary to" << dst;
                    } else {
                        qWarning() << "AIClientManager: Failed to archive summary to" << dst;
                    }
                }
            }
        } else {
            qWarning() << "AIClientManager: Meta-summary merge request failed:" << responseText;
        }
        m_mergingSessionIds.clear();
        processPendingRequests();
        return;
    }

    if (m_isTranslationRequest) {
        m_isTranslationRequest = false;
        if (success) {
            event.type = EventType::AIResponseReceived;
            event.text = applyMask(responseText);
            // Discord/Twitch宛てであれば返信先情報を設定（通常応答と同じ扱い）
            if (!m_currentDiscordChannelId.isEmpty()) {
                event.extraData["channel_id"] = m_currentDiscordChannelId;
            } else if (!m_currentTwitchChannel.isEmpty()) {
                event.extraData["twitch_channel"] = m_currentTwitchChannel;
            }
        } else {
            event.type = EventType::ErrorOccurred;
            event.text = responseText;
        }
        emit notifyEvent(event);
        clearRequestState();
        processPendingRequests();
        return;
    }

    if (m_isResetting) {
        m_isResetting = false;
        
        QString summaryStr = responseText;
        QStringList keywordsList;

        if (success) {
            // AI応答をパース
            int kwIdx = responseText.indexOf("Keywords:", 0, Qt::CaseInsensitive);
            int sumIdx = responseText.indexOf("Summary:", 0, Qt::CaseInsensitive);
            if (kwIdx != -1 && sumIdx != -1) {
                QString keywordsStr;
                if (kwIdx < sumIdx) {
                    keywordsStr = responseText.mid(kwIdx + 9, sumIdx - (kwIdx + 9)).trimmed();
                    summaryStr = responseText.mid(sumIdx + 8).trimmed();
                } else {
                    summaryStr = responseText.mid(sumIdx + 8, kwIdx - (sumIdx + 8)).trimmed();
                    keywordsStr = responseText.mid(kwIdx + 9).trimmed();
                }

                // キーワードの分解
                for (const QString &k : keywordsStr.split(",")) {
                    QString tk = k.trimmed();
                    if (!tk.isEmpty()) {
                        keywordsList.append(tk);
                    }
                }
            } else {
                keywordsList.append("対話");
                keywordsList.append("セッション");
            }

            // session_context.md の更新
            saveSessionContext(summaryStr);
        } else {
            qWarning() << "AIClientManager: Context summarization failed:" << responseText;
            keywordsList.append("エラー");
            summaryStr = "会話要約の生成に失敗しました。";
        }

        // 2. サマリメタデータファイルの保存 (log/archive/summary_<session_id>.json)
        QDir().mkpath("log/archive");
        QJsonObject summaryObj;
        summaryObj["session_id"] = m_currentResetSessionId;
        
        QJsonObject timeRange;
        timeRange["start"] = m_currentResetStartTime;
        timeRange["end"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        summaryObj["time_range"] = timeRange;
        
        QJsonArray kwArr;
        for (const QString &kw : keywordsList) {
            kwArr.append(kw);
        }
        summaryObj["keywords"] = kwArr;
        summaryObj["summary"] = summaryStr;

        QString summaryPath = QString("log/archive/summary_%1.json").arg(m_currentResetSessionId);
        QFile summaryFile(summaryPath);
        if (summaryFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QJsonDocument doc(summaryObj);
            summaryFile.write(doc.toJson());
            summaryFile.close();
            qDebug() << "AIClientManager: Saved session summary to" << summaryPath;
        } else {
            qWarning() << "AIClientManager: Failed to write session summary to" << summaryPath;
        }

        // 次回セッションの開始日時を更新
        m_currentResetStartTime = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

        // メモリ上の m_chatHistory を暗号化バックアップ (log/session_backup_<timestamp>.enc)
        QJsonArray histArray;
        for (const auto &pair : m_chatHistory) {
            QJsonObject entry;
            entry["prompt"] = pair.first;
            entry["response"] = pair.second;
            histArray.append(entry);
        }
        QJsonDocument doc(histArray);
        QByteArray rawData = doc.toJson(QJsonDocument::Compact);

        CipherResult result = CipherEngine::encrypt(rawData, m_transCipherKey, AesMode::Mandatory);
        if (result.isSuccess()) {
            QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
            QString filename = QString("log/session_backup_%1.enc").arg(timestamp);
            QFile file(filename);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(result.data());
                file.close();
                qDebug() << "AIClientManager: Saved encrypted session backup during context summary to" << filename;
            } else {
                qWarning() << "AIClientManager: Failed to write session backup file:" << filename;
            }
        } else {
            qWarning() << "AIClientManager: Encryption failed during reset:" << result.message();
        }

        // 履歴を完全にクリア
        m_chatHistory.clear();
        emit chatHistoryUpdated(m_chatHistory);

        // 個別サマリが溜まった場合の自動階層マージ（メタサマリ生成）判定
        checkAndMergeSummaries();

        // 通知イベントを送信
        if (m_isManualReset) {
            event.type = success ? EventType::AIResponseReceived : EventType::ErrorOccurred;
            event.text = success ? "会話履歴をクリアし、長期記憶サマリを生成しました。" : "会話履歴をクリアしましたが、サマリの保存に失敗しました。";
            emit notifyEvent(event);
        }
        processPendingRequests();
        return;
    }

    // 通常の会話応答
    if (success) {
        event.type = EventType::AIResponseReceived;
        
        QString cleanText = responseText;

        // 全 AI プロバイダ共通: LaTeX 数式コマンドの自動変換・文字化け展開クレンジング
        cleanText.replace(QRegularExpression("\\\\times"), "×");
        cleanText.replace(QRegularExpression("\\\\div"), "÷");
        cleanText.replace(QRegularExpression("\\\\text\\s*\\{\\s*([^\\}]+)\\s*\\}"), "\\1");
        cleanText.remove("\\[");
        cleanText.remove("\\]");
        cleanText.remove("\\(");
        cleanText.remove("\\)");

        // 疑似ファンクション呼び出しタグ (<function=...>...</function>) の自動パース・実行 & 除去
        static const QRegularExpression funcRegex("<function=([^>]+)>(.*?)</function>", QRegularExpression::DotMatchesEverythingOption);
        QRegularExpressionMatchIterator it = funcRegex.globalMatch(cleanText);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            QString funcName = match.captured(1).trimmed();
            QString funcArgsRaw = match.captured(2).trimmed();

            qDebug() << "AIClientManager: Detected pseudo function tag in response text. Function:" << funcName << "Args:" << funcArgsRaw;

            if (funcName == "update_nickname") {
                QJsonDocument argDoc = QJsonDocument::fromJson(funcArgsRaw.toUtf8());
                if (argDoc.isObject()) {
                    QJsonObject argObj = argDoc.object();
                    QString targetUser = argObj.value("target_user").toString();
                    QString nickname = argObj.value("nickname").toString();

                    if (targetUser.isEmpty()) {
                        targetUser = m_currentRequester;
                    }

                    if (!targetUser.isEmpty() && !nickname.isEmpty()) {
                        qDebug() << "AIClientManager: Executing pseudo update_nickname call for target:" << targetUser << "nickname:" << nickname;
                        handleNicknameUpdateRequest(targetUser, nickname);
                    }
                }
            }
        }

        // タグを応答テキストから完全消去
        cleanText.remove(funcRegex);
        cleanText = cleanText.trimmed();

        // 応答テキストにマスク（伏字）処理を適用
        QString filteredResponse = applyMask(cleanText);

        if (m_isShoutoutRequest) {
            m_isShoutoutRequest = false;
            if (!m_shoutoutPrefix.isEmpty() && !filteredResponse.startsWith(m_shoutoutPrefix)) {
                filteredResponse = m_shoutoutPrefix + " " + filteredResponse;
            }

            // F-22-1: Twitch Helix API 経由での背景カラーアナウンスバナー優先発火 (ハイブリッド制御)
            if (m_shoutoutUseAnnounce && m_helixClient) {
                QString color = m_shoutoutAnnounceColor;
                if (color == "random") {
                    static const QStringList colors = {"primary", "blue", "green", "orange", "purple"};
                    color = colors.at(QRandomGenerator::global()->bounded(colors.size()));
                }
                QString bId = m_twitchChannel.isEmpty() ? m_currentTwitchChannel : m_twitchChannel;
                QString mId = m_twitchUsername.isEmpty() ? bId : m_twitchUsername;
                m_helixClient->sendChatAnnouncement(bId, mId, filteredResponse, color);
            }
            // ※ IRC (PRIVMSG) 送信用テキストからは /announce コマンド文字を完全に削除し、
            // 純粋なチャットコメントとして Twitch チャット欄へ 100% 確実に投稿・表示させる
        }

        event.text = filteredResponse;

        // Discord / Twitch宛てであれば返信先情報を設定
        if (!m_currentDiscordChannelId.isEmpty()) {
            event.extraData["channel_id"] = m_currentDiscordChannelId;
        } else if (!m_currentTwitchChannel.isEmpty()) {
            event.extraData["twitch_channel"] = m_currentTwitchChannel;
        }

        // 履歴にペアを追加し、シグナルで通知
        m_chatHistory.append(QPair<QString, QString>(m_lastPromptWithTag, filteredResponse));
        emit chatHistoryUpdated(m_chatHistory);

        // 【TransCipher難読化要件の適用】
        // 会話ログを暗号化（難読化）してローカルファイルに保存する（従来のブロック蓄積）
        QString logText = QString("[%1] Prompt: %2 -> Response: %3")
                            .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
                            .arg(m_lastPromptWithTag)
                            .arg(filteredResponse);
        saveObfuscatedLog(logText);

        emit notifyEvent(event);

        // 会話履歴数が10件（5往復＝5ペア）に到達した場合は自動的かつサイレントにリセット
        if (m_chatHistory.size() >= 5) {
            qDebug() << "AIClientManager: Chat history reached limit (5 pairs / 10 messages). Triggering auto-reset...";
            resetSession(false);
        }

    } else {
        // --- F-33: エラーハンドリング ---
        // 1. JSON エラーボディのパース（OpenRouter 等が返す構造化エラー）
        QString errMessage;
        QString providerName;
        QString rawProviderError;
        int providerErrCode = 0;

        QJsonDocument errDoc = QJsonDocument::fromJson(responseText.toUtf8());
        if (errDoc.isObject()) {
            QJsonObject errRoot = errDoc.object();
            QJsonObject errObj = errRoot.value("error").toObject();
            errMessage = errObj.value("message").toString();
            QJsonObject meta = errObj.value("metadata").toObject();
            providerName = meta.value("provider_name").toString();
            rawProviderError = meta.value("raw").toString();
            providerErrCode = meta.value("provider_error_code").toString().toInt();
        }

        // 2. 詳細ログ出力（F-33-3: ログへは生エラー詳細を全文出力）
        QString currentClientId = m_currentClient ? m_currentClient->clientId() : "unknown";
        qWarning() << QString("[AIClientManager] HTTP Error %1 from %2").arg(httpCode).arg(currentClientId);
        if (!errMessage.isEmpty())
            qWarning() << "  error.message   :" << errMessage;
        if (!providerName.isEmpty())
            qWarning() << "  provider_name   :" << providerName;
        if (providerErrCode > 0)
            qWarning() << "  provider_code   :" << providerErrCode;
        if (!rawProviderError.isEmpty())
            qWarning() << "  raw             :" << rawProviderError;
        if (errMessage.isEmpty() && providerName.isEmpty())
            qWarning() << "  raw response    :" << responseText.left(300);

        // 3. 一時エラー（429/503）→ フォールバック試行（F-33-1/2）
        bool isTemporaryError = (httpCode == 429 || httpCode == 503);
        if (isTemporaryError && m_currentClient) {
            // 現プロバイダをレートリミット扱いにする（429発生時は学習適応制御を適用）
            if (httpCode == 429) {
                m_tracker.adaptOnHttp429(currentClientId, 60);
            } else {
                m_tracker.forceRateLimit(currentClientId, 60);
            }
            emit rateLimitStatusUpdated(m_tracker.allStatuses()); // レートリミット発生を即時通知


            if (m_fallbackIndex < m_fallbackProviders.size()) {
                // 次のフォールバック先を選択
                QString nextProvider = m_fallbackProviders.at(m_fallbackIndex);
                ++m_fallbackIndex;

                if (m_clientMap.contains(nextProvider)) {
                    m_currentClient = m_clientMap[nextProvider];
                    m_apiCallStartTimeMs = QDateTime::currentMSecsSinceEpoch();

                    // F-33-3 UI 警告（自然言語）
                    QString displayProvider = providerName.isEmpty() ? currentClientId : providerName;
                    QString uiWarn = buildHumanReadableError(httpCode, currentClientId,
                                                             errDoc.isObject() ? errDoc.object() : QJsonObject());
                    qDebug() << "AIClientManager: Fallback triggered." << currentClientId
                             << "->" << nextProvider << "UI msg:" << uiWarn;

                    AppEvent warnEvent;
                    warnEvent.type = EventType::AIResponseReceived;
                    warnEvent.source = "AIClientManager";
                    warnEvent.text = uiWarn;
                    emit notifyEvent(warnEvent);

                    // 元プロンプトをそのまま次プロバイダへ再送
                    m_currentClient->sendRequest(m_lastFinalPrompt, m_chatHistory,
                                                  m_sessionContext, m_lastAdditionalSystemPrompt);
                    return;
                }
            }

            event.type = EventType::ErrorOccurred;
            event.text = "❌ 全ての AI プロバイダがレート制限中です。しばらく待ってから再試行してください。";
            emit notifyEvent(event);
            clearRequestState();
            processPendingRequests();
            return;
        }

        // 4. 恒久エラー（400/401/403/404 等）→ 自然言語メッセージで即時通知（フォールバックなし）
        event.type = EventType::ErrorOccurred;
        event.text = buildHumanReadableError(httpCode, currentClientId,
                                             errDoc.isObject() ? errDoc.object() : QJsonObject());
        emit notifyEvent(event);
    }
    clearRequestState();
    processPendingRequests();
}

void AIClientManager::clearRequestState() {
    m_currentDiscordChannelId.clear();
    m_currentTwitchChannel.clear();
    m_currentRequester.clear();
}

void AIClientManager::resetSession(bool isManual) {
    if (m_chatHistory.isEmpty()) {
        qDebug() << "AIClientManager: resetSession called but history is empty. Nothing to reset.";
        if (isManual) {
            AppEvent event;
            event.type = EventType::AIResponseReceived;
            event.source = "AIClientManager";
            event.text = "会話履歴はすでにクリアされています。";
            emit notifyEvent(event);
        }
        processPendingRequests();
        return;
    }

    // セッションIDとタイムスタンプの決定
    m_currentResetSessionId = QString("session_%1").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
    QString endTime = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    qDebug() << "AIClientManager: Archiving detailed log for session:" << m_currentResetSessionId;

    // 1. 詳細ログの保存 (log/archive/detail_<session_id>.json)
    QDir().mkpath("log/archive");
    QJsonObject detailObj;
    detailObj["session_id"] = m_currentResetSessionId;

    QJsonArray histArray;
    for (const auto &pair : m_chatHistory) {
        QJsonObject userMsg;
        QJsonObject aiMsg;

        QString userPrompt = pair.first;
        QString aiResponse = pair.second;

        // ユーザー発言のパース
        QString source = "[User]";
        QString message = userPrompt;
        if (userPrompt.startsWith("[")) {
            int closeBracketIdx = userPrompt.indexOf(']');
            if (closeBracketIdx != -1) {
                QString tag = userPrompt.left(closeBracketIdx + 1);
                int colonIdx = userPrompt.indexOf(':', closeBracketIdx + 1);
                if (colonIdx != -1 && (tag == "[Discord]" || tag == "[Twitch]")) {
                    source = tag + " " + userPrompt.mid(closeBracketIdx + 1, colonIdx - (closeBracketIdx + 1)).trimmed();
                    message = userPrompt.mid(colonIdx + 1).trimmed();
                } else {
                    source = tag;
                    message = userPrompt.mid(closeBracketIdx + 1).trimmed();
                }
            }
        }

        userMsg["source"] = source;
        userMsg["message"] = message;
        userMsg["timestamp"] = m_currentResetStartTime;

        aiMsg["source"] = "[AI]";
        aiMsg["message"] = aiResponse;
        aiMsg["timestamp"] = endTime;

        histArray.append(userMsg);
        histArray.append(aiMsg);
    }
    detailObj["chat_history"] = histArray;

    QString detailPath = QString("log/archive/detail_%1.json").arg(m_currentResetSessionId);
    QFile detailFile(detailPath);
    if (detailFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QJsonDocument doc(detailObj);
        detailFile.write(doc.toJson());
        detailFile.close();
        qDebug() << "AIClientManager: Saved detailed session log to" << detailPath;
    } else {
        qWarning() << "AIClientManager: Failed to write detailed session log to" << detailPath;
    }

    qDebug() << "AIClientManager: Requesting AI to summarize conversation context. Manual:" << isManual;

    m_isResetting = true;
    m_isManualReset = isManual;

    // UIへ送信開始イベントを通知
    if (isManual) {
        AppEvent event;
        event.type = EventType::AIRequestSent;
        event.source = "AIClientManager";
        event.text = "これまでの会話履歴から要約および検索用キーワードを抽出し、長期記憶アーカイブを生成しています...";
        emit notifyEvent(event);
    }

    if (selectAndPrepareClient()) {
        // AIに要約とキーワードの抽出を依頼する特別なプロンプト
        QString summaryPrompt = 
            "これまでの対話から、主要なトピックキーワード（3〜5個）をカンマ区切りで抽出し、さらに会話の簡潔な要約（2〜3文）を作成してください。\n"
            "形式は必ず以下の通りにしてください（これ以外の挨拶や余計な説明文章は絶対に含めないでください）：\n"
            "Keywords: キーワード1, キーワード2, キーワード3\n"
            "Summary: 要約文";
        
        m_currentClient->sendRequest(summaryPrompt, m_chatHistory, "");
    } else {
        m_isResetting = false;
        processPendingRequests();
    }
}

void AIClientManager::saveObfuscatedLog(const QString &logText) {
    QDir().mkpath("log");
    QString logFilePath = "log/chat_history.enc";

    QByteArray rawData = logText.toUtf8();

    // TransCipherを使用してデータを暗号化
    CipherResult result = CipherEngine::encrypt(rawData, m_transCipherKey, AesMode::Mandatory);

    if (result.isSuccess()) {
        QFile file(logFilePath);
        // 追記モードで開く
        if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
            // 暗号化されたバイナリデータと区切り行を保存
            file.write(result.data());
            file.write("\n---END_BLOCK---\n");
            file.close();
            qDebug() << "AIClientManager: Obfuscated log saved successfully via TransCipher.";
        }
    } else {
        qWarning() << "AIClientManager: Failed to encrypt log via TransCipher:" << result.message();
    }
}

QList<QPair<QString, QString>> AIClientManager::loadObfuscatedBackup(const QString &filePath) {
    QList<QPair<QString, QString>> history;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "AIClientManager: Failed to open backup file for reading:" << filePath;
        return history;
    }
    QByteArray encryptedData = file.readAll();
    file.close();

    // TransCipherを使用してデータを復号
    CipherResult result = CipherEngine::decrypt(encryptedData, m_transCipherKey);
    if (!result.isSuccess()) {
        qWarning() << "AIClientManager: Failed to decrypt backup via TransCipher:" << result.message();
        return history;
    }

    QJsonDocument doc = QJsonDocument::fromJson(result.data());
    if (doc.isNull() || !doc.isArray()) {
        qWarning() << "AIClientManager: Decrypted data is not a valid JSON array.";
        return history;
    }

    QJsonArray array = doc.array();
    for (int i = 0; i < array.size(); ++i) {
        QJsonObject entry = array.at(i).toObject();
        QString prompt = entry.value("prompt").toString();
        QString response = entry.value("response").toString();
        history.append(QPair<QString, QString>(prompt, response));
    }

    qDebug() << "AIClientManager: Successfully loaded and decrypted" << history.size() << "turns from" << filePath;
    return history;
}

bool AIClientManager::importSessionBackup(const QString &filePath) {
    QList<QPair<QString, QString>> history = loadObfuscatedBackup(filePath);
    AppEvent event;
    event.source = "AIClientManager";

    if (history.isEmpty()) {
        event.type = EventType::ErrorOccurred;
        event.text = "会話履歴のインポートに失敗しました。（ファイルが空、または復号エラー）";
        emit notifyEvent(event);
        return false;
    }

    m_chatHistory = history;
    emit chatHistoryUpdated(m_chatHistory);
    
    // コンテキストファイルの再ロード
    loadSessionContext();

    qDebug() << "AIClientManager: Successfully imported conversation history from" << filePath;
    event.type = EventType::AIResponseReceived;
    event.text = "会話履歴をインポートしました。";
    emit notifyEvent(event);
    return true;
}

void AIClientManager::exportSessionBackup(const QString &encPath, const QString &txtPath) {
    QList<QPair<QString, QString>> history = loadObfuscatedBackup(encPath);
    
    AppEvent event;
    event.source = "AIClientManager";

    if (history.isEmpty()) {
        qWarning() << "AIClientManager: Export failed. Backup is empty or failed to decrypt:" << encPath;
        event.type = EventType::ErrorOccurred;
        event.text = "会話履歴のエクスポートに失敗しました。（ファイルが空、または復号エラー）";
        emit notifyEvent(event);
        return;
    }

    QFile file(txtPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "AIClientManager: Failed to open file for export writing:" << txtPath;
        event.type = EventType::ErrorOccurred;
        event.text = "エクスポート先のファイルを開けませんでした。";
        emit notifyEvent(event);
        return;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << "=== 会話履歴エクスポート (復号済) ===\n\n";
    int index = 1;
    for (const auto &pair : history) {
        out << QString("[会話 %1]\n").arg(index++);
        out << "ユーザー: " << pair.first << "\n";
        out << "AI: " << pair.second << "\n";
        out << "--------------------------------------------------\n\n";
    }
    file.close();

    qDebug() << "AIClientManager: Successfully exported decrypted history to" << txtPath;
    event.type = EventType::AIResponseReceived;
    event.text = QString("会話履歴をエクスポートしました:\n%1").arg(QFileInfo(txtPath).fileName());
    emit notifyEvent(event);
}

void AIClientManager::on_settingsUpdated() {
    qDebug() << "AIClientManager: Settings updated. Reloading credentials.";
    loadCredentials();
    loadBlacklist();
    loadWhitelist();
    
    // setAIProvider(m_provider, true) の内部で、最新の local_settings.json から
    // 各 API キーやモデル名の適用が自動的に行われます。
    setAIProvider(m_provider, true);
}

void AIClientManager::saveUserNames() {
    QString path = QCoreApplication::applicationDirPath() + "/Config/user_names.json";
    if (!QFile::exists(path) && QFile::exists("Config/user_names.json")) {
        path = "Config/user_names.json";
    }
#ifdef PROJECT_SOURCE_DIR
    if (!QFile::exists(path) && QFile::exists(QString(PROJECT_SOURCE_DIR) + "/Config/user_names.json")) {
        path = QString(PROJECT_SOURCE_DIR) + "/Config/user_names.json";
    }
    if (!QFile::exists(path) && QFile::exists(QString(PROJECT_SOURCE_DIR) + "/user_names.json")) {
        path = QString(PROJECT_SOURCE_DIR) + "/user_names.json";
    }
#endif
    if (!QFile::exists(path) && QFile::exists("user_names.json")) {
        path = "user_names.json";
    }
    if (!QFile::exists(path) && QFile::exists(QCoreApplication::applicationDirPath() + "/user_names.json")) {
        path = QCoreApplication::applicationDirPath() + "/user_names.json";
    }

    QFileInfo fileInfo(path);
    QDir().mkpath(fileInfo.absolutePath());

    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QJsonDocument doc(m_userNamesObj);
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
        qDebug() << "AIClientManager: Saved user names data to" << path;
        emit userNamesUpdated(m_userNamesObj); // UIに更新を通知
    } else {
        qWarning() << "AIClientManager: Failed to write user names data to" << path;
    }
}

QString AIClientManager::handleNicknameUpdateRequest(const QString &target, const QString &nickname) {
    QString targetLower = target.trimmed().toLower();
    QString nickTrimmed = nickname.trimmed();
    QString requester = m_currentRequester;

    qDebug() << "AIClientManager: Processing handleNicknameUpdateRequest. Requester:" << requester 
             << "Target:" << targetLower << "Proposed Nickname:" << nickTrimmed;

    if (targetLower.isEmpty() || nickTrimmed.isEmpty()) {
        return "Error: Invalid target user or nickname.";
    }

    // 1. 自動登録ケース (申請者 == 対象者 または 申請者 == 配信主)
    if (requester == targetLower || (!m_streamerName.isEmpty() && requester == m_streamerName)) {
        QJsonObject usersMap = m_userNamesObj.value("users").toObject();
        QJsonObject userData = usersMap.value(targetLower).toObject();
        
        // preferred を更新
        userData["preferred"] = nickTrimmed;
        
        // nicknames 配列にも追加しておく
        QJsonArray nicknamesArray = userData.value("nicknames").toArray();
        bool found = false;
        for (const QJsonValue &val : nicknamesArray) {
            if (val.toString().trimmed().toLower() == nickTrimmed.toLower()) {
                found = true;
                break;
            }
        }
        if (!found) {
            nicknamesArray.append(nickTrimmed);
        }
        userData["nicknames"] = nicknamesArray;
        
        usersMap[targetLower] = userData;
        m_userNamesObj["users"] = usersMap;
        
        saveUserNames();
        
        return QString("Success: Automatically registered '%1' as preferred nickname for '%2'.")
            .arg(nickTrimmed).arg(targetLower);
    }
    
    // 2. 他人による他人の設定ケース（承認待ち保留の判定）
    qDebug() << "AIClientManager: Processing nickname request for another user. Requester:" << requester 
             << "Target:" << targetLower << "Nickname:" << nickTrimmed;

    QJsonArray pendingList = m_userNamesObj.value("pending_requests").toArray();
    bool exists = false;
    for (const QJsonValue &val : pendingList) {
        QJsonObject req = val.toObject();
        if (req.value("requester").toString().toLower() == requester.toLower() &&
            req.value("target").toString().toLower() == targetLower &&
            req.value("nickname").toString().toLower() == nickTrimmed.toLower()) {
            exists = true;
            break;
        }
    }

    if (!exists) {
        QJsonObject reqObj;
        reqObj["requester"] = requester;
        reqObj["target"] = targetLower;
        reqObj["nickname"] = nickTrimmed;
        reqObj["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        pendingList.append(reqObj);
        m_userNamesObj["pending_requests"] = pendingList;
        saveUserNames();
    }

    return "Notification: The nickname update request has been submitted to the streamer for approval.";
}

void AIClientManager::approveNicknameRequest(const QString &requester, const QString &target, const QString &nickname) {
    QString targetLower = target.trimmed().toLower();
    QString nickTrimmed = nickname.trimmed();

    qDebug() << "AIClientManager: Streamer approved nickname request. Requester:" << requester 
             << "Target:" << targetLower << "Nickname:" << nickTrimmed;

    // 1. users マップに適用
    QJsonObject usersMap = m_userNamesObj.value("users").toObject();
    QJsonObject userData = usersMap.value(targetLower).toObject();
    
    userData["preferred"] = nickTrimmed;
    QJsonArray nicknamesArray = userData.value("nicknames").toArray();
    bool found = false;
    for (const QJsonValue &val : nicknamesArray) {
        if (val.toString().trimmed().toLower() == nickTrimmed.toLower()) {
            found = true;
            break;
        }
    }
    if (!found) {
        nicknamesArray.append(nickTrimmed);
    }
    userData["nicknames"] = nicknamesArray;
    
    usersMap[targetLower] = userData;
    m_userNamesObj["users"] = usersMap;

    // 2. pending_requests から該当を削除
    QJsonArray pendingList = m_userNamesObj.value("pending_requests").toArray();
    QJsonArray newPendingList;
    for (const QJsonValue &val : pendingList) {
        QJsonObject req = val.toObject();
        if (req.value("requester").toString().toLower() == requester.toLower() &&
            req.value("target").toString().toLower() == targetLower) {
            continue;
        }
        newPendingList.append(req);
    }
    m_userNamesObj["pending_requests"] = newPendingList;

    saveUserNames();
}

void AIClientManager::rejectNicknameRequest(const QString &requester, const QString &target, const QString &nickname) {
    QString targetLower = target.trimmed().toLower();

    qDebug() << "AIClientManager: Streamer rejected nickname request. Requester:" << requester 
             << "Target:" << targetLower << "Nickname:" << nickname;

    // pending_requests から該当を削除
    QJsonArray pendingList = m_userNamesObj.value("pending_requests").toArray();
    QJsonArray newPendingList;
    for (const QJsonValue &val : pendingList) {
        QJsonObject req = val.toObject();
        if (req.value("requester").toString().toLower() == requester.toLower() &&
            req.value("target").toString().toLower() == targetLower) {
            continue;
        }
        newPendingList.append(req);
    }
    m_userNamesObj["pending_requests"] = newPendingList;

    saveUserNames();
}

void AIClientManager::deleteNickname(const QString &user) {
    QString userLower = user.trimmed().toLower();
    qDebug() << "AIClientManager: Deleting nickname data for user:" << userLower;

    QJsonObject usersMap = m_userNamesObj.value("users").toObject();
    if (usersMap.contains(userLower)) {
        usersMap.remove(userLower);
        m_userNamesObj["users"] = usersMap;
        saveUserNames();
    }
}

void AIClientManager::updateNicknamePreferred(const QString &user, const QString &preferred) {
    QString userLower = user.trimmed().toLower();
    QString prefTrimmed = preferred.trimmed();
    qDebug() << "AIClientManager: Updating preferred name for user:" << userLower << "to:" << prefTrimmed;

    QJsonObject usersMap = m_userNamesObj.value("users").toObject();
    QJsonObject userData = usersMap.value(userLower).toObject();
    
    userData["preferred"] = prefTrimmed;
    
    // nicknames 配列にも追加しておく
    if (!prefTrimmed.isEmpty()) {
        QJsonArray nicknamesArray = userData.value("nicknames").toArray();
        bool found = false;
        for (const QJsonValue &val : nicknamesArray) {
            if (val.toString().trimmed().toLower() == prefTrimmed.toLower()) {
                found = true;
                break;
            }
        }
        if (!found) {
            nicknamesArray.append(prefTrimmed);
        }
        userData["nicknames"] = nicknamesArray;
    }
    
    usersMap[userLower] = userData;
    m_userNamesObj["users"] = usersMap;
    
    saveUserNames();
}

QJsonObject AIClientManager::findUserProfile(const QString &userLower, QString *outProfileKey) const {
    QJsonObject usersMap = m_userNamesObj.value("users").toObject();
    if (userLower.isEmpty()) return QJsonObject();

    if (usersMap.contains(userLower)) {
        if (outProfileKey) *outProfileKey = userLower;
        return usersMap.value(userLower).toObject();
    }

    for (auto it = usersMap.begin(); it != usersMap.end(); ++it) {
        QJsonObject obj = it.value().toObject();
        QString tid = obj.value("twitch_id").toString().trimmed().toLower();
        QString did = obj.value("discord_id").toString().trimmed().toLower();
        if ((!tid.isEmpty() && tid == userLower) || (!did.isEmpty() && did == userLower)) {
            if (outProfileKey) *outProfileKey = it.key();
            return obj;
        }
    }

    if (outProfileKey) *outProfileKey = QString();
    return QJsonObject();
}

void AIClientManager::updateUserMapping(const QString &profileId, const QString &preferred, const QString &twitchId, const QString &discordId, const QStringList &nicknames) {
    QString pKey = profileId.trimmed().toLower();
    if (pKey.isEmpty()) return;

    QString tId = twitchId.trimmed().toLower();
    QString dId = discordId.trimmed().toLower();
    QString pref = preferred.trimmed();

    QJsonObject usersMap = m_userNamesObj.value("users").toObject();
    QJsonObject userData = usersMap.value(pKey).toObject();

    userData["preferred"] = pref;
    if (!tId.isEmpty()) userData["twitch_id"] = tId;
    if (!dId.isEmpty()) userData["discord_id"] = dId;

    if (!nicknames.isEmpty()) {
        QJsonArray arr;
        for (const QString &n : nicknames) {
            if (!n.trimmed().isEmpty()) arr.append(n.trimmed());
        }
        userData["nicknames"] = arr;
    }

    usersMap[pKey] = userData;
    m_userNamesObj["users"] = usersMap;

    // 重複レコードの自動検出 & マージ
    QString duplicateKey;
    if (!tId.isEmpty() || !dId.isEmpty()) {
        for (auto it = usersMap.begin(); it != usersMap.end(); ++it) {
            if (it.key() == pKey) continue;
            QJsonObject other = it.value().toObject();
            QString oTid = other.value("twitch_id").toString().trimmed().toLower();
            QString oDid = other.value("discord_id").toString().trimmed().toLower();
            if ((!tId.isEmpty() && (it.key() == tId || oTid == tId)) ||
                (!dId.isEmpty() && (it.key() == dId || oDid == dId))) {
                duplicateKey = it.key();
                break;
            }
        }
    }

    if (!duplicateKey.isEmpty()) {
        mergeUserProfiles(pKey, duplicateKey);
    } else {
        saveUserNames();
    }
}

void AIClientManager::mergeUserProfiles(const QString &targetProfileId, const QString &sourceProfileId) {
    QString targetKey = targetProfileId.trimmed().toLower();
    QString sourceKey = sourceProfileId.trimmed().toLower();
    if (targetKey.isEmpty() || sourceKey.isEmpty() || targetKey == sourceKey) return;

    QJsonObject usersMap = m_userNamesObj.value("users").toObject();
    if (!usersMap.contains(targetKey) || !usersMap.contains(sourceKey)) return;

    QJsonObject targetData = usersMap.value(targetKey).toObject();
    QJsonObject sourceData = usersMap.value(sourceKey).toObject();

    if (targetData.value("preferred").toString().trimmed().isEmpty()) {
        targetData["preferred"] = sourceData.value("preferred").toString().trimmed();
    }
    if (targetData.value("twitch_id").toString().trimmed().isEmpty()) {
        targetData["twitch_id"] = sourceData.value("twitch_id").toString().trimmed();
    }
    if (targetData.value("discord_id").toString().trimmed().isEmpty()) {
        targetData["discord_id"] = sourceData.value("discord_id").toString().trimmed();
    }

    QJsonArray tArr = targetData.value("nicknames").toArray();
    QJsonArray sArr = sourceData.value("nicknames").toArray();
    QSet<QString> nSet;
    for (const QJsonValue &v : tArr) nSet.insert(v.toString().trimmed());
    for (const QJsonValue &v : sArr) nSet.insert(v.toString().trimmed());
    QJsonArray newArr;
    for (const QString &n : nSet) {
        if (!n.isEmpty()) newArr.append(n);
    }
    targetData["nicknames"] = newArr;

    usersMap[targetKey] = targetData;
    usersMap.remove(sourceKey);

    m_userNamesObj["users"] = usersMap;
    saveUserNames();
}

void AIClientManager::checkAndMergeSummaries() {
    QDir dir("log/archive");
    if (!dir.exists()) return;

    QStringList filters;
    filters << "summary_*.json";
    QFileInfoList fileList = dir.entryInfoList(filters, QDir::Files, QDir::Name); // 名前順 (日付順)

    if (fileList.size() < 10) {
        return;
    }

    qDebug() << "AIClientManager: Found" << fileList.size() << "summaries. Triggering meta-summary merge.";

    m_isMergingSummaries = true;
    m_mergingSessionIds.clear();

    QString combinedText;
    int count = 0;
    for (const QFileInfo &fileInfo : fileList) {
        if (count >= 10) break;
        
        QFile file(fileInfo.absoluteFilePath());
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QByteArray data = file.readAll();
            file.close();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (!doc.isNull() && doc.isObject()) {
                QJsonObject obj = doc.object();
                QString sessionId = obj.value("session_id").toString();
                QString summary = obj.value("summary").toString();
                QJsonArray kwArr = obj.value("keywords").toArray();
                QStringList kws;
                for (const QJsonValue &v : kwArr) kws.append(v.toString());

                combinedText += QString("Session ID: %1 (Keywords: %2)\nSummary: %3\n\n")
                                    .arg(sessionId, kws.join(", "), summary);
                
                m_mergingSessionIds.append(sessionId);
                count++;
            }
        }
    }

    if (!m_mergingSessionIds.isEmpty() && selectAndPrepareClient()) {
        QString mergePrompt = 
            "以下は、過去のいくつかの対話セッションのサマリ情報です。これらを統合し、この期間全体の主要トピックを表す1つの「マージ要約（メタサマリ）」と、全体を代表するキーワード（5〜8個）を抽出してください。\n"
            "形式は必ず以下の通りにしてください（余計な挨拶や説明は絶対に含めないでください）：\n"
            "Keywords: キーワード1, キーワード2, キーワード3\n"
            "Summary: 要約文\n\n"
            "--- 対話セッションサマリ群 ---\n" + combinedText;

        m_currentClient->sendRequest(mergePrompt, QList<QPair<QString, QString>>(), "");
    } else {
        m_isMergingSummaries = false;
        processPendingRequests();
    }
}

void AIClientManager::scanMemorySummaries(const QString &prompt) {
    // 過去想起を示すトリガーワードの検知
    QStringList triggerWords = { "過去", "以前", "前話した", "前言った", "前回の会話", "昔", "覚えている", "おぼえている", "記憶" };
    bool triggered = false;
    for (const QString &tw : triggerWords) {
        if (prompt.contains(tw)) {
            triggered = true;
            break;
        }
    }

    if (!triggered) {
        return;
    }

    QDir archiveDir("log/archive");
    if (!archiveDir.exists()) return;

    QString bestSessionId;
    int bestScore = 0;
    QString targetMetaFile;

    // --- 第一段階: メタサマリ & 未マージサマリのスキャン ---
    // A. メタサマリのスキャン
    QStringList metaFilters;
    metaFilters << "meta_summary_*.json";
    QFileInfoList metaFiles = archiveDir.entryInfoList(metaFilters, QDir::Files);
    for (const QFileInfo &fileInfo : metaFiles) {
        QFile file(fileInfo.absoluteFilePath());
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
            file.close();
            
            QJsonArray kwArr = obj.value("keywords").toArray();
            int score = 0;
            for (const QJsonValue &v : kwArr) {
                if (prompt.contains(v.toString(), Qt::CaseInsensitive)) {
                    score += 2;
                }
            }
            if (prompt.contains(obj.value("meta_summary").toString(), Qt::CaseInsensitive)) {
                score += 1;
            }

            if (score > bestScore) {
                bestScore = score;
                bestSessionId = obj.value("meta_id").toString();
                targetMetaFile = fileInfo.absoluteFilePath();
            }
        }
    }

    // B. 未マージサマリのスキャン
    QStringList sumFilters;
    sumFilters << "summary_*.json";
    QFileInfoList sumFiles = archiveDir.entryInfoList(sumFilters, QDir::Files);
    bool hitMeta = false;
    if (bestScore > 0 && bestSessionId.startsWith("meta_")) {
        hitMeta = true;
    }

    for (const QFileInfo &fileInfo : sumFiles) {
        QFile file(fileInfo.absoluteFilePath());
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
            file.close();

            QJsonArray kwArr = obj.value("keywords").toArray();
            int score = 0;
            for (const QJsonValue &v : kwArr) {
                if (prompt.contains(v.toString(), Qt::CaseInsensitive)) {
                    score += 2;
                }
            }
            if (prompt.contains(obj.value("summary").toString(), Qt::CaseInsensitive)) {
                score += 1;
            }

            if (score > bestScore) {
                bestScore = score;
                bestSessionId = obj.value("session_id").toString();
                hitMeta = false;
            }
        }
    }

    // --- 第二段階: メタサマリ配下の個別サミリスキャン (メタサマリがヒットした場合) ---
    if (hitMeta && !targetMetaFile.isEmpty()) {
        QFile file(targetMetaFile);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
            file.close();

            QJsonArray childSessions = obj.value("child_sessions").toArray();
            QString bestChildSession;
            int bestChildScore = 0;

            QDir childDir("log/archive/archived_summaries");
            for (const QJsonValue &v : childSessions) {
                QString sid = v.toString();
                QString childPath = childDir.absoluteFilePath(QString("summary_%1.json").arg(sid));
                QFile cfile(childPath);
                if (cfile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    QJsonObject cobj = QJsonDocument::fromJson(cfile.readAll()).object();
                    cfile.close();

                    QJsonArray ckwArr = cobj.value("keywords").toArray();
                    int score = 0;
                    for (const QJsonValue &cv : ckwArr) {
                        if (prompt.contains(cv.toString(), Qt::CaseInsensitive)) {
                            score += 2;
                        }
                    }
                    if (prompt.contains(cobj.value("summary").toString(), Qt::CaseInsensitive)) {
                        score += 1;
                    }

                    if (score >= bestChildScore) {
                        bestChildScore = score;
                        bestChildSession = sid;
                    }
                }
            }

            if (!bestChildSession.isEmpty()) {
                bestSessionId = bestChildSession;
            }
        }
    }

    if (bestScore > 0 && !bestSessionId.isEmpty() && !bestSessionId.startsWith("meta_")) {
        qDebug() << "AIClientManager: Recalled session identified:" << bestSessionId << "with score:" << bestScore;
        loadMemoryDetail(bestSessionId);
    }
}

void AIClientManager::loadMemoryDetail(const QString &sessionId) {
    QString detailPath = QString("log/archive/detail_%1.json").arg(sessionId);
    QFile file(detailPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "AIClientManager: Recalled detail file not found:" << detailPath;
        return;
    }

    QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
    file.close();

    QJsonArray chatHistory = obj.value("chat_history").toArray();
    if (chatHistory.isEmpty()) return;

    QString recalledText = QString("[過去の関連会話の記憶 (セッションID: %1):\n").arg(sessionId);
    int startIdx = qMax(0, chatHistory.size() - 6);
    for (int i = startIdx; i < chatHistory.size(); ++i) {
        QJsonObject msgObj = chatHistory[i].toObject();
        QString source = msgObj.value("source").toString();
        QString msg = msgObj.value("message").toString();
        recalledText += QString("%1: %2\n").arg(source, msg);
    }
    recalledText += "]";

    m_recalledContext = recalledText;
    qDebug() << "AIClientManager: Injected memory context size:" << m_recalledContext.length() << "chars.";
}

void AIClientManager::loadKnowledgeMetadata() {
    QDir().mkpath("log/knowledge");
    QString path = "log/knowledge/knowledge_metadata.json";
    QFile file(path);
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QByteArray data = file.readAll();
        file.close();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isNull() && doc.isObject()) {
            m_knowledgeMetadata = doc.object();
            qDebug() << "AIClientManager: Loaded knowledge metadata from" << path;
        } else {
            m_knowledgeMetadata = QJsonObject();
            qWarning() << "AIClientManager: Invalid format in knowledge_metadata.json. Starting fresh.";
        }
    } else {
        m_knowledgeMetadata = QJsonObject();
    }
    emit knowledgeMetadataUpdated(m_knowledgeMetadata);
}

void AIClientManager::saveKnowledgeMetadata() {
    QDir().mkpath("log/knowledge");
    QString path = "log/knowledge/knowledge_metadata.json";
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QJsonDocument doc(m_knowledgeMetadata);
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
        qDebug() << "AIClientManager: Saved knowledge metadata to" << path;
        emit knowledgeMetadataUpdated(m_knowledgeMetadata);
    } else {
        qWarning() << "AIClientManager: Failed to write knowledge metadata to" << path;
    }
}

void AIClientManager::scanStaticKnowledge(const QString &prompt, QString &recalledPrompt) {
    QJsonArray knowledges = m_knowledgeMetadata.value("knowledges").toArray();
    QString injectedKnowledge;
    for (const QJsonValue &val : knowledges) {
        QJsonObject obj = val.toObject();
        QJsonArray kwArr = obj.value("keywords").toArray();
        bool hit = false;
        for (const QJsonValue &kwVal : kwArr) {
            // 完全一致または単語境界チェック等の簡易判定（promptに含まれるか）
            QString kw = kwVal.toString().trimmed();
            if (!kw.isEmpty() && prompt.contains(kw, Qt::CaseInsensitive)) {
                hit = true;
                break;
            }
        }
        if (hit) {
            QString fileName = obj.value("file_name").toString();
            QFile file("log/knowledge/" + fileName);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QString content = QString::fromUtf8(file.readAll());
                file.close();
                injectedKnowledge += QString("[関連知識: %1]\n%2\n\n")
                                        .arg(obj.value("title").toString(), content);
                qDebug() << "AIClientManager: Recalled static knowledge:" << obj.value("title").toString();
            }
        }
    }
    if (!injectedKnowledge.isEmpty()) {
        recalledPrompt = injectedKnowledge + recalledPrompt;
    }
}

void AIClientManager::deleteKnowledge(const QString &id) {
    qDebug() << "AIClientManager: deleteKnowledge called for id:" << id;
    QJsonArray knowledges = m_knowledgeMetadata.value("knowledges").toArray();
    QJsonArray newKnowledges;
    bool found = false;

    for (int i = 0; i < knowledges.size(); ++i) {
        QJsonObject entry = knowledges.at(i).toObject();
        if (entry.value("id").toString() == id) {
            QString fileName = entry.value("file_name").toString();
            QFile::remove("log/knowledge/" + fileName);
            found = true;
            qDebug() << "AIClientManager: Deleted knowledge file:" << fileName;
            continue;
        }
        newKnowledges.append(entry);
    }

    if (found) {
        m_knowledgeMetadata["knowledges"] = newKnowledges;
        saveKnowledgeMetadata();
    } else {
        qWarning() << "AIClientManager: Knowledge id not found for deletion:" << id;
    }
}

void AIClientManager::on_requestKnowledgeMetadata() {
    loadKnowledgeMetadata();
}

void AIClientManager::onImportTimeout() {
    qDebug() << "AIClientManager: Import timeout fired.";
    if (m_importState == KnowledgeImportState::AwaitingFileAndExplanation) {
        m_importState = KnowledgeImportState::CancelConfirmation;
        AppEvent event;
        event.source = "AIClientManager";
        event.type = EventType::AIResponseReceived;
        event.text = "ナレッジフォルダを開いてから10分が経過しましたが、登録の動きがありません。このまま登録作業をキャンセルしますか？（キャンセルする場合は「はい」または `/cancel` と入力してください）";
        emit notifyEvent(event);
    }
}

QString AIClientManager::finalizeKnowledgeImport(const QString &title, const QString &description, const QStringList &keywords) {
    qDebug() << "AIClientManager: finalizeKnowledgeImport called with title:" << title
             << "description:" << description << "keywords:" << keywords;

    if (m_importingFileName.isEmpty() || m_importState != KnowledgeImportState::QandAMode) {
        return "Error: No active knowledge import session to finalize.";
    }

    QDir().mkpath("log/knowledge");

    // ユニークIDの生成
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    QString knowledgeId = QString("knowledge_%1").arg(timestamp);
    QString newFileName = QString("%1.md").arg(knowledgeId);

    // 一時フォルダからコピー
    QString srcPath = "log/knowledge_input/" + m_importingFileName;
    QString dstPath = "log/knowledge/" + newFileName;

    if (!QFile::copy(srcPath, dstPath)) {
        qWarning() << "AIClientManager: Failed to copy knowledge file from" << srcPath << "to" << dstPath;
        return "Error: Failed to copy Markdown file to permanent storage.";
    }

    // 元の一時ファイルを削除（クリーンアップ）
    QFile::remove(srcPath);

    // メタデータの更新
    QJsonArray knowledges = m_knowledgeMetadata.value("knowledges").toArray();
    
    QJsonObject newEntry;
    newEntry["id"] = knowledgeId;
    newEntry["title"] = title;
    newEntry["description"] = description;
    
    QJsonArray kwArr;
    for (const QString &kw : keywords) {
        if (!kw.trimmed().isEmpty()) {
            kwArr.append(kw.trimmed());
        }
    }
    newEntry["keywords"] = kwArr;
    newEntry["file_name"] = newFileName;
    newEntry["registered_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    knowledges.append(newEntry);
    m_knowledgeMetadata["knowledges"] = knowledges;

    saveKnowledgeMetadata();

    // 状態のリセット
    m_importState = KnowledgeImportState::Idle;
    m_importingFileName.clear();
    m_importingFileContent.clear();

    return "Success: Knowledge registered successfully.";
}

void AIClientManager::openKnowledgeInputFolder() {
    QDir().mkpath("log/knowledge_input");
    QString absPath = QDir("log/knowledge_input").absolutePath();
    qDebug() << "AIClientManager: Opening knowledge input folder:" << absPath;
    QUrl url = QUrl::fromLocalFile(absPath);
    QMetaObject::invokeMethod(qApp, [url]() {
        QDesktopServices::openUrl(url);
    }, Qt::QueuedConnection);
}

void AIClientManager::processPendingRequests() {
    if (m_isResetting || m_isMergingSummaries) {
        return;
    }

    if (!m_pendingRequests.isEmpty()) {
        PendingRequest req = m_pendingRequests.takeFirst();
        qDebug() << "AIClientManager: Processing queued pending request for user:" << req.user;
        on_requestAI(req.prompt, req.user);
    }
}

// ---- F-33 ここから ----

void AIClientManager::buildFallbackProviderList() {
    // API キー設定済みプロバイダを優先順に並べ、現在選択中のプロバイダを除外する
    // 優先順序: groq → cerebras → mistral → huggingface → openrouter → sakura
    static const QStringList priorityOrder = {
        "groq", "cerebras", "mistral", "huggingface", "openrouter", "sakura"
    };
    m_fallbackProviders.clear();

    QString activeManagerProvider = (m_managerEnabled && !m_managerProvider.isEmpty()) ? m_managerProvider : QString();
    bool managerProviderAddedAsTail = false;

    for (const QString &id : priorityOrder) {
        if (id == m_provider) continue;            // 選択中 Worker プロバイダはスキップ
        if (!m_clientMap.contains(id)) continue;   // 存在しないプロバイダはスキップ

        // マネージャー AI で使用中のプロバイダは最下位（末尾）に回すため、通常スキャンからは一旦スキップ
        if (!activeManagerProvider.isEmpty() && id == activeManagerProvider) {
            managerProviderAddedAsTail = true;
            continue;
        }

        ProviderStatus s = m_tracker.statusOf(id);
        if (s.available) {                         // APIキー設定済みのみ
            m_fallbackProviders.append(id);
        }
    }

    // マネージャー AI プロバイダが存在し、APIキー設定済みかつ現在 Worker として選択されていない場合、最下位に追加
    if (managerProviderAddedAsTail && m_clientMap.contains(activeManagerProvider) && activeManagerProvider != m_provider) {
        ProviderStatus s = m_tracker.statusOf(activeManagerProvider);
        if (s.available) {
            m_fallbackProviders.append(activeManagerProvider);
        }
    }

    qDebug() << "AIClientManager: Fallback provider list built (Manager AI provider demoted to tail):" << m_fallbackProviders;
}


QString AIClientManager::buildHumanReadableError(int httpCode, const QString &providerId,
                                                  const QJsonObject &errorJson) const {
    // JSON からメタ情報を抽出
    QString errMsg    = errorJson.value("error").toObject().value("message").toString();
    QString provName  = errorJson.value("error").toObject()
                                 .value("metadata").toObject()
                                 .value("provider_name").toString();
    QString rawErr    = errorJson.value("error").toObject()
                                 .value("metadata").toObject()
                                 .value("raw").toString();

    // フォールバック先プロバイダ名（次の候補）
    QString nextProvider;
    if (m_fallbackIndex < m_fallbackProviders.size()) {
        nextProvider = m_fallbackProviders.at(m_fallbackIndex);
    }

    // 自然言語メッセージ生成
    QString displayProvider = provName.isEmpty() ? providerId : provName;

    switch (httpCode) {
    case 429: {
        if (!nextProvider.isEmpty()) {
            return QString("⚠️ %1 がレート制限中のため、%2 に自動切り替えしました")
                       .arg(displayProvider, nextProvider);
        } else {
            return QString("⚠️ %1 がレート制限中です。しばらく待ってから再試行してください")
                       .arg(displayProvider);
        }
    }
    case 503:
        if (!nextProvider.isEmpty()) {
            return QString("⚠️ %1 が一時的に利用できないため、%2 に自動切り替えしました")
                       .arg(displayProvider, nextProvider);
        } else {
            return QString("⚠️ %1 が一時的に利用できません。しばらく待ってから再試行してください")
                       .arg(displayProvider);
        }
    case 401:
        return QString("❌ API キーが正しくありません (%1)。AI設定タブでキーを確認してください")
                   .arg(providerId);
    case 403:
        return QString("❌ API キーの権限が不足しています (%1)。AI設定タブでキーを確認してください")
                   .arg(providerId);
    case 404:
        return QString("❌ 指定したモデル名が見つかりません (%1)。AI設定タブでモデル名を確認してください")
                   .arg(providerId);
    case 400:
        return QString("❌ リクエストが無効です (%1): %2")
                   .arg(providerId, errMsg.isEmpty() ? "不正なリクエスト形式" : errMsg);
    default:
        if (httpCode > 0) {
            return QString("❌ AI エラー (HTTP %1, %2): %3")
                       .arg(httpCode).arg(providerId)
                       .arg(errMsg.isEmpty() ? rawErr.left(100) : errMsg);
        }
        // httpCode == 0 の場合（非 HTTP エラー）
        return QString("❌ AI 通信エラー (%1): %2")
                   .arg(providerId, errMsg.isEmpty() ? rawErr.left(100) : errMsg);
    }
}

// ---- F-33 ここまで ----

bool AIClientManager::selectAndPrepareClient(const QString &prompt) {
    QString targetProvider = m_provider;

    // 1. 全 API キーが未設定かどうかの検証
    bool hasAnyApiKey = false;
    QStringList unconfiguredProviders;
    QStringList allKnownProviders = {"mistral", "cerebras", "groq", "huggingface", "openrouter", "sakura"};
    for (const QString &p : allKnownProviders) {
        if (m_clientMap.contains(p)) {
            QString key = m_clientMap[p]->apiKey();
            if (!key.trimmed().isEmpty()) {
                hasAnyApiKey = true;
            } else {
                unconfiguredProviders.append(p);
            }
        }
    }

#ifndef QT_DEBUG
    if (targetProvider == "dummy" && hasAnyApiKey) {
        targetProvider = "auto";
    }
#endif

    if (!hasAnyApiKey && targetProvider != "dummy") {
        qWarning() << "AIClientManager: No API keys configured for any provider.";
        QString noKeyMsg = "※【APIキー未設定の通知】AIプロバイダのAPIキーが設定されていません。設定画面からAPIキーを入力してください。";
        AppEvent ev;
        ev.type = EventType::AIResponseReceived;
        ev.source = "AIClientManager";
        ev.text = noKeyMsg;
        if (!m_currentDiscordChannelId.isEmpty()) ev.extraData["channel_id"] = m_currentDiscordChannelId;
        else if (!m_currentTwitchChannel.isEmpty()) ev.extraData["twitch_channel"] = m_currentTwitchChannel;
        emit notifyEvent(ev);
        m_currentDiscordChannelId.clear();
        m_currentTwitchChannel.clear();
        return false;
    }

    // 2. ユーザー明示選択プロバイダの優先判定
    if (!targetProvider.isEmpty() && targetProvider != "auto" && m_clientMap.contains(targetProvider)) {
        if (targetProvider == "dummy" || m_tracker.isAvailable(targetProvider)) {
            m_currentClient = m_clientMap[targetProvider];
            qDebug() << "AIClientManager: Routing request directly to user-selected client:" << targetProvider;
            m_apiCallStartTimeMs = QDateTime::currentMSecsSinceEpoch();
            return true;
        }
    }

    // 3. 自動選定 ("auto" または 選択プロバイダがリミット中のフォールバック)
    QString bestClientId = m_tracker.selectBestAvailableClient();
    if (bestClientId.isEmpty() && targetProvider == "dummy") {
#ifdef QT_DEBUG
        bestClientId = "dummy";
#endif
    }

    if (bestClientId.isEmpty()) {
        auto resetInfo = m_tracker.earliestResetTime();
        int waitSecs = qMax(1, (int)QDateTime::currentDateTimeUtc().secsTo(resetInfo.resetAt));
        int minutes = waitSecs / 60;
        int seconds = waitSecs % 60;
        QString waitTimeStr = (minutes > 0) ? QString("約%1分%2秒").arg(minutes).arg(seconds) : QString("約%1秒").arg(seconds);

        QString exhaustionMsg;
        if (!unconfiguredProviders.isEmpty()) {
            // パターン A: 未設定キーが存在する場合
            exhaustionMsg = QString("※【レートリミット到達のお知らせ】現在設定されているすべてのAIプロバイダが利用上限（レートリミット）に達しています。解除まであと【%1】お待ちいただくか、未設定のAIプロバイダ（%2）のAPIキーを設定画面で追加登録することで、すぐに利用を再開できます。")
                                .arg(waitTimeStr, unconfiguredProviders.join(", "));
        } else {
            // パターン B: 全キー設定済みで全枯渇の場合
            exhaustionMsg = QString("※【レートリミット到達のお知らせ】すべてのAIプロバイダが利用上限（レートリミット）に達しています。リミットが解除されるまで【%1】ほどお待ちください。")
                                .arg(waitTimeStr);
        }

        qWarning() << "AIClientManager: All AI clients are rate-limited / unavailable:" << exhaustionMsg;

        AppEvent ev;
        ev.type = EventType::AIResponseReceived;
        ev.source = "AIClientManager";
        ev.text = exhaustionMsg;
        if (!m_currentDiscordChannelId.isEmpty()) ev.extraData["channel_id"] = m_currentDiscordChannelId;
        else if (!m_currentTwitchChannel.isEmpty()) ev.extraData["twitch_channel"] = m_currentTwitchChannel;
        emit notifyEvent(ev);
        m_currentDiscordChannelId.clear();
        m_currentTwitchChannel.clear();

        return false;
    }

    m_currentClient = m_clientMap[bestClientId];
    qDebug() << "AIClientManager: Auto-routed request to best available client:" << bestClientId;
    m_apiCallStartTimeMs = QDateTime::currentMSecsSinceEpoch();
    return true;
}

void AIClientManager::on_requestProviderStatus(const QString &providerId) {
    if (m_clientMap.contains(providerId)) {
        emit providerStatusResponse(m_tracker.statusOf(providerId));
    }
}

void AIClientManager::emitCurrentStatus() {
    // RateLimitTabWidget が接続された直後に初期状態を配信する（aiThread 上で実行される）
    emit rateLimitStatusUpdated(m_tracker.allStatuses());
}

QString AIClientManager::fetchSchedules(const QString &category, const QDate &startDate, int days) {
    if (m_taskFlowApiUrl.trimmed().isEmpty()) {
        qWarning() << "AIClientManager: TaskFlow API URL is not configured. Skipping schedule fetch.";
        return QString();
    }
    QNetworkAccessManager manager;
    QString baseUrl = m_taskFlowApiUrl.trimmed();
    QUrl url(baseUrl);
    QUrlQuery query;
    query.addQueryItem("category", category);
    query.addQueryItem("start_date", startDate.toString("yyyy-MM-dd"));
    query.addQueryItem("days", QString::number(days));
    url.setQuery(query);

    QNetworkRequest request(url);
    QNetworkReply *reply = manager.get(request);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(5000); // 5秒タイムアウト

    loop.exec();

    QString resultText;

    if (reply->isFinished() && reply->error() == QNetworkReply::NoError) {
        QByteArray responseData = reply->readAll();
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            QJsonObject root = doc.object();
            if (root.value("status").toString() == "success") {
                QJsonArray data = root.value("data").toArray();
                for (int i = 0; i < data.size(); ++i) {
                    QJsonObject task = data.at(i).toObject();
                    QString rawTitle = task.value("title").toString();
                    QString startAt = task.value("start_at").toString();
                    QString endAt = task.value("end_at").toString();
                    int progress = task.value("progress_rate").toInt();

                    // 曜日の取得とフォーマット整形
                    auto formatWithDay = [](const QString &dateTimeStr) -> QString {
                        QDateTime dt = QDateTime::fromString(dateTimeStr, "yyyy-MM-dd HH:mm:ss");
                        if (!dt.isValid()) {
                            dt = QDateTime::fromString(dateTimeStr, "yyyy-MM-dd HH:mm");
                        }
                        if (!dt.isValid()) {
                            dt = QDateTime::fromString(dateTimeStr, Qt::ISODate);
                        }
                        if (dt.isValid()) {
                            static const QStringList days = {"", "月", "火", "水", "木", "金", "土", "日"};
                            int dayOfWeek = dt.date().dayOfWeek();
                            QString dayStr = (dayOfWeek >= 1 && dayOfWeek <= 7) ? days.at(dayOfWeek) : "";
                            return QString("%1(%2) %3")
                                .arg(dt.toString("yyyy-MM-dd"))
                                .arg(dayStr)
                                .arg(dt.toString("HH:mm:ss"));
                        }
                        return dateTimeStr;
                    };

                    QString startFormatted = formatWithDay(startAt);
                    QString endFormatted = formatWithDay(endAt);

                    // TransCipherによる復号
                    QByteArray encrypted = QByteArray::fromBase64(rawTitle.toUtf8());
                    CipherResult decResult = CipherEngine::decrypt(encrypted, "test_secret_key_12345");
                    QString title = decResult.isSuccess() ? QString::fromUtf8(decResult.data()) : rawTitle;

                    resultText += QString("- タスク: %1\n  期間: %2 ~ %3\n  進捗率: %4%\n")
                                      .arg(title)
                                      .arg(startFormatted)
                                      .arg(endFormatted)
                                      .arg(progress);
                }
            } else {
                qWarning() << "AIClientManager: schedules API status not success:" << root.value("status").toString();
            }
        } else {
            qWarning() << "AIClientManager: schedules API response JSON parse error:" << parseError.errorString();
        }
    } else {
        qWarning() << "AIClientManager: schedules API request failed or timed out:" 
                   << (reply->isFinished() ? reply->errorString() : "Timeout");
    }

    reply->deleteLater();
    return resultText;
}

QString AIClientManager::getTaskFlowSchedulesContext() {
    QDate today = QDate::currentDate();

    // 今日から7日間のスケジュールを取得
    QString workSchedules = fetchSchedules("work", today, 7);
    QString streamSchedules = fetchSchedules("stream", today, 7);

    QString context = "\n[システム指示: 以下は外部APIから取得したユーザーの最新スケジュールと作業・配信の進捗状況です。ユーザーから今後の予定やタスク、進捗状況について尋ねられた場合は、提供された現在日時と以下のスケジュール情報をベースにして親切に回答してください。情報が存在しない（空である）場合は、予定が登録されていない旨を優しく伝えてください。]\n";
    context += "【作業・タスク予定 (work)】\n";
    context += workSchedules.isEmpty() ? "登録されている作業予定はありません。\n" : workSchedules;
    context += "\n【配信・ストリーム予定 (stream)】\n";
    context += streamSchedules.isEmpty() ? "登録されている配信予定はありません。\n" : streamSchedules;
    context += "\n";

    return context;
}

void AIClientManager::on_twitchRaidReceived(const QString &username) {
    qDebug() << "AIClientManager: Received Twitch Raid event for user:" << username;
    if (m_raidAutoShoutoutEnabled) {
        handleRaidShoutout(username);
    }
}

void AIClientManager::handleRaidShoutout(const QString &username) {
    if (username.isEmpty()) return;

    qDebug() << "AIClientManager: Handling raid shoutout for" << username;
    if (!m_helixClient) return;

    m_helixClient->fetchCreatorInfo(username, [this, username](const CreatorHelixInfo &info, bool success) {
        QString displayName = success ? info.displayName : username;
        QString bio = success ? info.description : "";
        QString game = success ? info.gameName : "";
        QString title = success ? info.title : "";
        QString sns = success ? info.snsInfo : "";

        // 紹介文生成用プロンプト構築
        QString prompt = QString(
            "あなたは配信アバターです。レイドしてくれたクリエイター「%1」さんの魅力を視聴者に紹介するコメントを作成してください。\n"
            "【クリエイター情報】\n"
            "- Twitch ID / 表示名: %2 / %1\n"
            "- 自己紹介 (Bio): %3\n"
            "- 直近の配信ゲーム/カテゴリ: %4\n"
            "- 配信タイトル: %5\n"
            "- 公式SNS/外部情報: %6\n\n"
            "【出力条件】\n"
            "- 長さ: %7\n"
            "- トーン・口調: %8\n"
            "- 感謝の気持ちを込めつつ、相手の配信を見に行きたくなるような明るい紹介文にしてください。"
        ).arg(displayName)
         .arg(username)
         .arg(bio.isEmpty() ? "情報なし" : bio)
         .arg(game.isEmpty() ? "ゲーム・カテゴリ情報なし" : game)
         .arg(title.isEmpty() ? "配信タイトルなし" : title)
         .arg(sns.isEmpty() ? "なし" : sns)
         .arg(m_shoutoutLength)
         .arg(m_shoutoutTone);

        // クライアントで紹介文を生成
        if (m_currentTwitchChannel.isEmpty()) {
            m_currentTwitchChannel = m_twitchChannel;
        }
        m_isShoutoutRequest = true;

        if (m_currentClient) {
            m_currentClient->sendRequest(prompt, {}, "", "シャウトアウト紹介コメントを生成してください。");
        } else if (m_dummyClient) {
            m_dummyClient->sendRequest(prompt, {}, "", "シャウトアウト紹介コメントを生成してください。");
        }

        // Twitch公式 /shoutout コマンド処理 (自分自身への /shoutout は Twitch 仕様上不可のためスキップ)
        bool isSelf = (!m_twitchChannel.isEmpty() && username.toLower() == m_twitchChannel.toLower()) ||
                     (!m_twitchUsername.isEmpty() && username.toLower() == m_twitchUsername.toLower());
        if (m_shoutoutUseCommand && !isSelf) {
            if (m_shoutoutCooldownTimer && m_shoutoutCooldownTimer->isActive()) {
                // クールタイム中のため待機キューに追加
                PendingShoutout ps;
                ps.username = username;
                ps.displayName = displayName;
                ps.requestTime = QDateTime::currentDateTime();
                m_shoutoutQueue.append(ps);
                qDebug() << "AIClientManager: Added shoutout to queue for" << username << "Queue size:" << m_shoutoutQueue.size();
                updateShoutoutUiStatus();
            } else {
                // 即時送信
                qDebug() << "AIClientManager: Sending immediate Twitch Helix Shoutout for" << username;
                m_lastShoutoutUser = username;
                triggerShoutout(username);

                if (m_shoutoutCooldownTimer) {
                    m_shoutoutCooldownTimer->start(120000);
                    m_shoutoutCooldownStartMs = QDateTime::currentMSecsSinceEpoch();
                    updateShoutoutUiStatus();
                }
            }
        }
    });
}

void AIClientManager::triggerShoutout(const QString &username) {
    if (m_helixClient) {
        QString fromUser = m_twitchChannel.isEmpty() ? m_currentTwitchChannel : m_twitchChannel;
        m_helixClient->sendShoutoutToUser(fromUser, username, [this, username](bool ok) {
            if (ok) {
                qDebug() << "AIClientManager: Twitch Helix Shoutout API succeeded for" << username;
                on_shoutoutSuccessReceived(username);
            } else {
                qWarning() << "AIClientManager: Twitch Helix Shoutout API failed for" << username;
            }
        });
    }
}

void AIClientManager::processNextShoutoutInQueue() {
    qDebug() << "AIClientManager: Shoutout cooldown expired.";
    m_shoutoutCooldownStartMs = 0;

    if (!m_shoutoutQueue.isEmpty()) {
        PendingShoutout ps = m_shoutoutQueue.takeFirst();
        qDebug() << "AIClientManager: Processing queued shoutout for" << ps.username;
        m_lastShoutoutUser = ps.username;

        triggerShoutout(ps.username);

        if (m_shoutoutCooldownTimer) {
            m_shoutoutCooldownTimer->start(120000);
            m_shoutoutCooldownStartMs = QDateTime::currentMSecsSinceEpoch();
        }
    }
    updateShoutoutUiStatus();
}

void AIClientManager::updateShoutoutUiStatus() {
    int remainingSec = 0;
    if (m_shoutoutCooldownTimer && m_shoutoutCooldownTimer->isActive()) {
        qint64 elapsedMs = QDateTime::currentMSecsSinceEpoch() - m_shoutoutCooldownStartMs;
        remainingSec = qMax(0, (int)((120000 - elapsedMs) / 1000));
    }

    AppEvent cdEv;
    cdEv.type = EventType::ShoutoutCooldownUpdated;
    cdEv.source = "AIClientManager";
    cdEv.text = QString::number(remainingSec);
    emit notifyEvent(cdEv);

    QStringList queueNames;
    for (const PendingShoutout &ps : m_shoutoutQueue) {
        queueNames.append(ps.displayName.isEmpty() ? ps.username : ps.displayName);
    }

    AppEvent qEv;
    qEv.type = EventType::ShoutoutQueueUpdated;
    qEv.source = "AIClientManager";
    qEv.extraData["queueList"] = queueNames;
    emit notifyEvent(qEv);
}

void AIClientManager::on_shoutoutSuccessReceived(const QString &username) {
    QString name = username.isEmpty() ? m_lastShoutoutUser : username;
    if (name.isEmpty()) return;

    qDebug() << "AIClientManager: Received Shoutout Success for" << name;
    if (m_shoutoutFollowMsgEnabled) {
        QString followMsg = m_shoutoutFollowMsgTemplate;
        followMsg.replace("{name}", name);
        followMsg.replace("{display_name}", name);

        qDebug() << "AIClientManager: Posting follow-up message:" << followMsg;

        AppEvent ev;
        ev.type = EventType::AIResponseReceived;
        ev.source = "ShoutoutModule";
        QString targetChannel = m_currentTwitchChannel.isEmpty() ? m_twitchChannel : m_currentTwitchChannel;
        if (!targetChannel.isEmpty()) {
            ev.extraData["twitch_channel"] = targetChannel;
        }
        if (m_shoutoutUseAnnounce) {
            QString color = m_shoutoutAnnounceColor;
            if (color == "random") {
                static const QStringList colors = {"primary", "blue", "green", "orange", "purple"};
                color = colors.at(QRandomGenerator::global()->bounded(colors.size()));
            }
            ev.text = QString("/announce %1 %2").arg(color).arg(followMsg);
        } else {
            ev.text = followMsg;
        }
        emit notifyEvent(ev);
    }
}

// ============================================================================
// 段階的タスク実行パイプライン実装 (Task Pipeline Implementation)
// ============================================================================

QString AIClientManager::generateRefinedQuery(const QString &rawQuerySentence, const QString &targetType) {
    QString text = rawQuerySentence;
    text.remove(" (weather)").remove(" (tide)");

    static const QStringList noisePatterns = {
        "を調べて", "調べて", "教えて", "教えてください", "知りたい", "知りたいんだけど",
        "雨が降るようなら", "晴れるようなら", "〜なら", "散歩をしたいんだけど", "散歩したい",
        "釣りに行きたい", "釣りに行こうかな", "〜したい", "〜しようかな", "について",
        "はどうですか", "はどう", "どうなってますか", "確認して", "検索して", "を調べたうえで"
    };
    for (const QString &noise : noisePatterns) {
        text.replace(noise, " ");
    }

    QDateTime now = QDateTime::currentDateTime();
    if (rawQuerySentence.contains("明日") || rawQuerySentence.contains("あした")) {
        now = now.addDays(1);
    }
    QString dateStr = now.toString("yyyy年M月d日");

    QStringList keywords;
    keywords.append(dateStr);

    if (rawQuerySentence.contains("神奈川")) keywords.append("神奈川県");
    else if (rawQuerySentence.contains("横浜")) keywords.append("横浜市");
    else if (rawQuerySentence.contains("東京")) keywords.append("東京都");
    else if (rawQuerySentence.contains("大阪")) keywords.append("大阪府");
    else if (rawQuerySentence.contains("愛知") || rawQuerySentence.contains("名古屋")) keywords.append("愛知県");

    if (targetType == "tide" || (rawQuerySentence.contains("潮") && !rawQuerySentence.contains("天気"))) {
        keywords.append("潮汐");
        keywords.append("満潮");
        keywords.append("干潮");
        keywords.append("潮見表");
    } else {
        keywords.append("天気");
        keywords.append("降水確率");
    }

    return keywords.join(" ").trimmed();
}

QList<AIClientManager::ExecutionTask> AIClientManager::analyzeAndDecomposeTasks(const QString &prompt) {
    QList<ExecutionTask> tasks;
    QString cleanedPrompt = prompt.trimmed();
    if (cleanedPrompt.isEmpty()) return tasks;

    bool hasWeather = cleanedPrompt.contains("天気") || cleanedPrompt.contains("てんき");
    bool hasTide = cleanedPrompt.contains("潮汐") || cleanedPrompt.contains("潮") || cleanedPrompt.contains("満潮") || cleanedPrompt.contains("干潮") || cleanedPrompt.contains("釣り");

    if (hasWeather && hasTide) {
        ExecutionTask task1;
        task1.type = TaskType::WebSearchRAG;
        task1.queryKeyword = cleanedPrompt + " (weather)";
        tasks.append(task1);

        ExecutionTask task2;
        task2.type = TaskType::WebSearchRAG;
        task2.queryKeyword = cleanedPrompt + " (tide)";
        tasks.append(task2);
        return tasks;
    }

    QStringList rawSegments = cleanedPrompt.split(QRegularExpression("[。！!？?\n|]|(と)|(および)|(あと)"), Qt::SkipEmptyParts);
    if (rawSegments.isEmpty()) {
        rawSegments.append(cleanedPrompt);
    }

    for (const QString &rawSeg : rawSegments) {
        QString seg = rawSeg.trimmed();
        if (seg.isEmpty() || seg.length() < 2) continue;

        QString lowerSeg = seg.toLower();

        // スケジュール・タスク関連キーワードが含まれる場合は Web 検索を即座に除外 (TaskFlow優先)
        bool isSchedule = lowerSeg.contains("予定") || lowerSeg.contains("スケジュール") ||
                          lowerSeg.contains("タスク") || lowerSeg.contains("カレンダー") ||
                          lowerSeg.contains("配信予定") || lowerSeg.contains("作業予定");

        // Web 検索トリガーは明確な情報目的名詞が含まれる場合のみ発火 (「今日」「明日」単体での誤発火を全廃)
        bool isWebSearch = !isSchedule && (
                           lowerSeg.contains("天気") || lowerSeg.contains("てんき") ||
                           lowerSeg.contains("気温") || lowerSeg.contains("降水") ||
                           lowerSeg.contains("ニュース") || lowerSeg.contains("最新ニュース") ||
                           lowerSeg.contains("速報") || lowerSeg.contains("為替") ||
                           lowerSeg.contains("株価") || lowerSeg.contains("ドル円") ||
                           lowerSeg.contains("日経") || lowerSeg.contains("wiki") ||
                           lowerSeg.contains("とは") || lowerSeg.contains("誰") ||
                           lowerSeg.contains("潮") || lowerSeg.contains("潮汐") || lowerSeg.contains("釣り") ||
                           lowerSeg.contains("weather") || lowerSeg.contains("news"));

        ExecutionTask task;
        if (isWebSearch) {
            task.type = TaskType::WebSearchRAG;
            task.queryKeyword = seg;
            tasks.append(task);
        } else {
            KnowledgeIndexEntry entry = m_tableEngine.resolveBestEntryForTrigger(seg);
            if (!entry.filePath.isEmpty()) {
                task.type = TaskType::KnowledgeSearch;
                task.queryKeyword = seg;
                tasks.append(task);
            }
        }
    }

    return tasks;
}

void AIClientManager::executeTaskPipeline(QList<ExecutionTask> &tasks) {
    for (auto &task : tasks) {
        if (task.type == TaskType::WebSearchRAG && m_searchManager) {
            QString targetType = task.queryKeyword.contains("(tide)") ? "tide" : "weather";
            QString refinedQuery = generateRefinedQuery(task.queryKeyword, targetType);
            qDebug() << "[AIClientManager] Pipeline executing WebSearchRAG task with refined query:" << refinedQuery << "(Original:" << task.queryKeyword << ")";
            QString rawResult = m_searchManager->executeSearchSync(refinedQuery);
            if (!rawResult.isEmpty()) {
                QString cleanText = rawResult;
                cleanText.remove(QRegularExpression("https?://\\S+"));
                cleanText.remove(QRegularExpression("URL:\\s*"));
                cleanText.remove(QRegularExpression("\\[\\d+\\]"));
                cleanText.remove(QRegularExpression("\\(\\s*\\)"));

                QStringList lines = cleanText.split('\n', Qt::SkipEmptyParts);
                QStringList filteredLines;
                for (const QString &rawLine : lines) {
                    QString line = rawLine.trimmed();
                    if (line.isEmpty()) continue;
                    if (line.contains("アメダス") || line.contains("ランキング") || line.contains("指数") ||
                        line.contains("花火") || line.contains("紫外線") || line.contains("洗濯") ||
                        line.contains("服装") || line.contains("星空") || line.contains("2週間") ||
                        line.contains("10日間") || line.contains("コイン") || line.contains("サイトマップ") ||
                        line.contains("ヘルプ") || line.contains("利用規約") || line.contains("プライバシー")) {
                        continue;
                    }
                    filteredLines.append(line);
                    if (filteredLines.size() >= 10) break;
                }
                task.extractedData = filteredLines.join("\n").trimmed();
                if (task.extractedData.isEmpty()) task.extractedData = cleanText.left(250);
                task.isCompleted = true;
            }
        } else if (task.type == TaskType::KnowledgeSearch) {
            qDebug() << "[AIClientManager] Pipeline executing KnowledgeSearch task for:" << task.queryKeyword;
            KnowledgeIndexEntry entry = m_tableEngine.resolveBestEntryForTrigger(task.queryKeyword);
            if (!entry.filePath.isEmpty()) {
                task.extractedData = QString("【ナレッジデータ (%1/%2)】: ファイル %3 (トリガー: %4)").arg(entry.category, entry.tableName, entry.filePath, entry.triggers.join(","));
                task.isCompleted = true;
            }
        }
    }
}

void AIClientManager::validateAndInjectGuards(const QList<ExecutionTask> &tasks, const QString &originalPrompt, QString &additionalSystemPrompt) {
    bool requestedTide = originalPrompt.contains("潮") || originalPrompt.contains("潮汐") || originalPrompt.contains("満潮") || originalPrompt.contains("干潮");
    if (!requestedTide) return;

    bool foundTideData = false;
    for (const auto &task : tasks) {
        if (task.extractedData.contains("満潮") || task.extractedData.contains("干潮") || task.extractedData.contains("潮位") || task.extractedData.contains("小潮") || task.extractedData.contains("大潮") || task.extractedData.contains("中潮")) {
            foundTideData = true;
            break;
        }
    }

    if (!foundTideData) {
        qDebug() << "[AIClientManager] Validator: User requested tide data but search results lacked tide info. Injecting hallucination guard constraint.";
        QString guardMsg = "※【重要・データ未取得の通知】今回の検索結果には指定された「潮汐（満潮・干潮の時刻）」の正確なデータが含まれていませんでした。時刻や数値を絶対に推測・捏造して回答せず、「潮汐データは取得できなかったため、正確な潮見表をご確認ください」と明記したうえで、取得できた天気情報のみに基づいて回答してください。";
        additionalSystemPrompt = guardMsg + "\n\n" + additionalSystemPrompt;
    }
}

QString AIClientManager::formatCombinedPrompt(const QList<ExecutionTask> &tasks, const QString &originalPrompt) {
    Q_UNUSED(originalPrompt);
    QStringList contextItems;
    int taskIdx = 1;
    for (const auto &task : tasks) {
        if (task.isCompleted && !task.extractedData.isEmpty()) {
            QString item = QString("■ [%1情報 (タスク%2)]:\n%3")
                               .arg(task.type == TaskType::WebSearchRAG ? "WebSearch" : "Knowledge")
                               .arg(taskIdx++)
                               .arg(task.extractedData);
            contextItems.append(item);
        }
    }

    if (contextItems.isEmpty()) {
        return QString();
    }

    QDateTime now = QDateTime::currentDateTime();
    return QString("【事前収集リファレンスデータ (現在日時: %1時点)】\n※以下の最新収集データを参考にして、ユーザーの質問に正確に回答してください：\n%2")
              .arg(now.toString("yyyy-MM-dd"))
              .arg(contextItems.join("\n\n"));
}

QString AIClientManager::formatSpeakerTaggedPrompt(const QString &prompt, const QString &speaker, const QString &target, const QString &categoryStr) {
    if (speaker.isEmpty() && categoryStr.isEmpty()) return prompt;
    QString tag = "[発言者: " + (speaker.isEmpty() ? "不明" : speaker);
    if (!categoryStr.isEmpty()) tag += " (" + categoryStr + ")";
    if (!target.isEmpty()) tag += " | 宛先: " + (target.isEmpty() ? "全体" : target);
    tag += "]";
    return tag + " " + prompt;
}



