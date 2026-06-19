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

AIClientManager::AIClientManager(QObject *parent)
    : QObject(parent), m_provider("dummy") 
{
    loadCredentials();
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
            m_transCipherKey = obj["trans_cipher_key"].toString("DefaultCipherKey123");
            m_provider = obj["ai_provider"].toString("dummy");
            qDebug() << "AIClientManager: Loaded settings from" << configPath;
        }
    }
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

    connect(m_currentClient, &IAIClient::requestFinished,
            this, &AIClientManager::on_clientRequestFinished);
}

void AIClientManager::on_requestAI(const QString &prompt) {
    qDebug() << "AIClientManager: Received request for prompt:" << prompt;

    m_lastPrompt = prompt;

    // コアへ送信開始イベントを通知
    AppEvent event;
    event.type = EventType::AIRequestSent;
    event.source = "AIClientManager";
    event.text = prompt;
    emit notifyEvent(event);

    if (m_currentClient) {
        m_currentClient->sendRequest(prompt, m_chatHistory, m_sessionContext);
    }
}

void AIClientManager::on_clientRequestFinished(const QString &responseText, bool success) {
    qDebug() << "AIClientManager: Client request finished. Success:" << success << "Resetting:" << m_isResetting;

    AppEvent event;
    event.source = "AIClientManager";

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
        event.text = responseText;

        // 履歴にペアを追加し、シグナルで通知
        m_chatHistory.append(QPair<QString, QString>(m_lastPrompt, responseText));
        emit chatHistoryUpdated(m_chatHistory);

        // 【TransCipher難読化要件の適用】
        // 会話ログを暗号化（難読化）してローカルファイルに保存する（従来のブロック蓄積）
        QString logText = QString("[%1] Prompt: %2 -> Response: %3")
                            .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
                            .arg(m_lastPrompt)
                            .arg(responseText);
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

void AIClientManager::on_requestChatHistory() {
    qDebug() << "AIClientManager: Processing chat history request. History size:" << m_chatHistory.size();

    QString md;
    if (m_chatHistory.isEmpty()) {
        md = "会話履歴はまだありません。";
    } else {
        md = "## 会話履歴\n\n";
        for (int i = 0; i < m_chatHistory.size(); ++i) {
            const auto &pair = m_chatHistory.at(i);
            md += QString("### [%1]\n").arg(i + 1);
            md += QString("**ユーザー**:\n%1\n\n").arg(pair.first);
            md += QString("**AI**:\n%1\n\n").arg(pair.second);
            md += "---\n\n";
        }
    }

    AppEvent event;
    event.type = EventType::ChatHistoryReceived;
    event.source = "AIClientManager";
    event.text = md;
    emit notifyEvent(event);
}
