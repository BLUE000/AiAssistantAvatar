#include "ai_client_manager.h"
#include "mistral_ai_client.h"
#include "dummy_ai_client.h"
#include "cipher_engine.h" // TransCipher
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QDateTime>
#include <QCoreApplication>
#include <QDebug>
#include <QTextStream>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>

AIClientManager::AIClientManager(QObject *parent)
    : QObject(parent), m_provider(ConfigDefaults::AI_PROVIDER) 
{
    loadCredentials();
    loadBlacklist();
    loadWhitelist();
    loadSessionContext();
    setAIProvider(m_provider); // ロードされたプロバイダを設定
}

AIClientManager::~AIClientManager() {
    delete m_currentClient;
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

void AIClientManager::loadCredentials() {
    QString configPath = "local_settings.json";
#ifdef PROJECT_SOURCE_DIR
    if (!QFile::exists(configPath)) {
        configPath = QString(PROJECT_SOURCE_DIR) + "/local_settings.json";
    }
#endif
    if (!QFile::exists(configPath)) {
        configPath = QCoreApplication::applicationDirPath() + "/local_settings.json";
    }
    if (!QFile::exists(configPath)) {
        configPath = QCoreApplication::applicationDirPath() + "/../local_settings.json";
    }
    if (!QFile::exists(configPath)) {
        configPath = QCoreApplication::applicationDirPath() + "/../../local_settings.json";
    }

    if (!QFile::exists(configPath)) {
        qWarning() << "AIClientManager: local_settings.json does not exist. Using empty settings. Tried path:" << configPath;
        return;
    }

    QFile file(configPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QByteArray data = file.readAll();
        file.close();

        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject obj = doc.object();
            m_apiKey = obj["mistral_api_key"].toString();
            m_tavilyApiKey = obj["tavily_api_key"].toString();
            m_transCipherKey = obj["trans_cipher_key"].toString("DefaultCipherKey123");
            m_provider = obj["ai_provider"].toString(ConfigDefaults::AI_PROVIDER);
            m_blacklistEnabled = obj.value("blacklist_enabled").toBool(true);
            qDebug() << "AIClientManager: Loaded settings from" << configPath << "Blacklist enabled:" << m_blacklistEnabled;
        }
    }
}

void AIClientManager::loadBlacklist() {
    m_blacklist.clear();
    if (!m_blacklistEnabled) {
        return;
    }

    QString blacklistPath = "blacklist.txt";
#ifdef PROJECT_SOURCE_DIR
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

    QString whitelistPath = "whitelist.txt";
#ifdef PROJECT_SOURCE_DIR
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

void AIClientManager::setAIProvider(const QString &provider) {
    if (m_currentClient && m_provider == provider) return;

    qDebug() << "AIClientManager: Changing AI Provider to" << provider;

    if (m_currentClient) {
        m_currentClient->disconnect(this);
        delete m_currentClient;
        m_currentClient = nullptr;
    }

    m_provider = provider;
    if (provider == "mistral") {
        m_currentClient = new MistralAIClient(this);
    } else {
        m_currentClient = new DummyAIClient(this);
    }

    m_currentClient->setApiKey(m_apiKey);
    m_currentClient->setTavilyApiKey(m_tavilyApiKey);

    connect(m_currentClient, &IAIClient::requestFinished,
            this, &AIClientManager::on_clientRequestFinished);
}

void AIClientManager::on_requestAI(const QString &prompt) {
    qDebug() << "AIClientManager: Received request for prompt:" << prompt;

    QString filteredPrompt = applyMask(prompt);
    QString trimmedPrompt = filteredPrompt.trimmed();

    // 翻訳コマンドの判定 ("trans" で始まるか)
    if (trimmedPrompt.startsWith("trans", Qt::CaseInsensitive)) {
        QString cmdArgs = trimmedPrompt.mid(5).trimmed(); // "trans" の後
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

        // コアへ送信開始イベントを通知
        AppEvent event;
        event.type = EventType::AIRequestSent;
        event.source = "AIClientManager";
        event.text = filteredPrompt;
        emit notifyEvent(event);

        if (m_currentClient) {
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
    m_lastPrompt = filteredPrompt;

    // コアへ送信開始イベントを通知
    AppEvent event;
    event.type = EventType::AIRequestSent;
    event.source = "AIClientManager";
    event.text = filteredPrompt;
    emit notifyEvent(event);

    if (m_currentClient) {
        m_currentClient->sendRequest(filteredPrompt, m_chatHistory, m_sessionContext);
    }
}

void AIClientManager::on_clientRequestFinished(const QString &responseText, bool success) {
    qDebug() << "AIClientManager: Client request finished. Success:" << success << "Resetting:" << m_isResetting << "Translation:" << m_isTranslationRequest;

    AppEvent event;
    event.source = "AIClientManager";

    if (m_isTranslationRequest) {
        m_isTranslationRequest = false;
        if (success) {
            event.type = EventType::AIResponseReceived;
            event.text = applyMask(responseText);
        } else {
            event.type = EventType::ErrorOccurred;
            event.text = responseText;
        }
        emit notifyEvent(event);
        return;
    }

    if (m_isResetting) {
        m_isResetting = false;
        if (success) {
            // AIから返ってきた要約を session_context.md に平文保存
            saveSessionContext(responseText);
        } else {
            qWarning() << "AIClientManager: Context summarization failed:" << responseText;
        }

        // メモリ上の m_chatHistory を暗号化バックアップ (log/session_backup_<timestamp>.enc)
        QDir().mkpath("log");
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

        // 通知イベントを送信
        if (m_isManualReset) {
            event.type = success ? EventType::AIResponseReceived : EventType::ErrorOccurred;
            event.text = success ? "会話履歴をクリアし、コンテキスト要約を保存しました。" : "会話履歴をクリアしましたが、要約の保存に失敗しました。";
            emit notifyEvent(event);
        }
        return;
    }

    // 通常の会話応答
    if (success) {
        event.type = EventType::AIResponseReceived;
        
        // 応答テキストにマスク（伏字）処理を適用
        QString filteredResponse = applyMask(responseText);
        event.text = filteredResponse;

        // 履歴にペアを追加し、シグナルで通知
        m_chatHistory.append(QPair<QString, QString>(m_lastPrompt, filteredResponse));
        emit chatHistoryUpdated(m_chatHistory);

        // 【TransCipher難読化要件の適用】
        // 会話ログを暗号化（難読化）してローカルファイルに保存する（従来のブロック蓄積）
        QString logText = QString("[%1] Prompt: %2 -> Response: %3")
                            .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
                            .arg(m_lastPrompt)
                            .arg(filteredResponse);
        saveObfuscatedLog(logText);

        emit notifyEvent(event);

        // 会話履歴数が10件（5往復＝5ペア）に到達した場合は自動的かつサイレントにリセット
        if (m_chatHistory.size() >= 5) {
            qDebug() << "AIClientManager: Chat history reached limit (5 pairs / 10 messages). Triggering auto-reset...";
            resetSession(false);
        }

    } else {
        event.type = EventType::ErrorOccurred;
        event.text = responseText; // エラーメッセージが格納されている
        emit notifyEvent(event);
    }
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
        return;
    }

    qDebug() << "AIClientManager: Requesting AI to summarize conversation context. Manual:" << isManual;

    m_isResetting = true;
    m_isManualReset = isManual;

    // UIへ送信開始イベントを通知
    if (isManual) {
        AppEvent event;
        event.type = EventType::AIRequestSent;
        event.source = "AIClientManager";
        event.text = "これまでの会話履歴から、今後の会話に引き継ぐべきコンテキスト（ユーザーの関心事、キャラクター設定、要約など）をマークダウン形式で要約しています...";
        emit notifyEvent(event);
    }

    if (m_currentClient) {
        // AIに要約を求める。プロンプトとして指示。履歴も一緒に渡す。
        QString summaryPrompt = 
            "これまでの対話から、今後の会話に引き継ぐべきコンテキスト（ユーザーの関心事、キャラクター設定、要約など）をマークダウン形式で簡潔にまとめてください。"
            "余計な前置きや挨拶、締めくくりの言葉などは一切省き、マークダウンのみを出力してください。";
        // 要約リクエスト時には sessionContext は空にする
        m_currentClient->sendRequest(summaryPrompt, m_chatHistory, "");
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
    setAIProvider(m_provider);
    if (m_currentClient) {
        m_currentClient->setApiKey(m_apiKey);
        m_currentClient->setTavilyApiKey(m_tavilyApiKey);
    }
}
