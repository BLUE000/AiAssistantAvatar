#include "ai_client_manager.h"
#include "mistral_ai_client.h"
#include "dummy_ai_client.h"
#include "cipher_engine.h" // TransCipher
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QDateTime>
#include <QDebug>

AIClientManager::AIClientManager(QObject *parent)
    : QObject(parent), m_provider("dummy") 
{
    loadCredentials();
    setAIProvider("dummy"); // 初期はdummy、準備ができたら設定ファイル等に基づき設定
}

AIClientManager::~AIClientManager() {
    delete m_currentClient;
}

void AIClientManager::loadCredentials() {
    QString configPath = "local_settings.json";
    if (!QFile::exists(configPath)) {
        qWarning() << "AIClientManager: local_settings.json does not exist. Using empty settings.";
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
            qDebug() << "AIClientManager: Loaded settings from local_settings.json";
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

    // コアへ送信開始イベントを通知
    AppEvent event;
    event.type = EventType::AIRequestSent;
    event.source = "AIClientManager";
    event.text = prompt;
    emit notifyEvent(event);

    if (m_currentClient) {
        m_currentClient->sendRequest(prompt);
    }
}

void AIClientManager::on_clientRequestFinished(const QString &responseText, bool success) {
    qDebug() << "AIClientManager: Client request finished. Success:" << success;

    AppEvent event;
    event.source = "AIClientManager";

    if (success) {
        event.type = EventType::AIResponseReceived;
        event.text = responseText;

        // 【TransCipher難読化要件の適用】
        // 会話ログを暗号化（難読化）してローカルファイルに保存する
        QString logText = QString("[%1] Prompt -> Response: %2")
                            .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
                            .arg(responseText);
        saveObfuscatedLog(logText);

    } else {
        event.type = EventType::ErrorOccurred;
        event.text = responseText; // エラーメッセージが格納されている
    }

    emit notifyEvent(event);
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
