#include "huggingface_ai_client.h"
#include "ai_client_manager.h"
#include "../search/search_manager.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QDebug>

HuggingFaceAIClient::HuggingFaceAIClient(QObject *parent)
    : IAIClient(parent), m_isToolCalling(false), m_model("meta-llama/Llama-3.1-8B-Instruct")
{
    m_networkManager = new QNetworkAccessManager(this);
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &HuggingFaceAIClient::on_networkReplyFinished);

    m_searchManager = new SearchManager(this);
    connect(m_searchManager, &SearchManager::searchFinished,
            this, &HuggingFaceAIClient::on_searchFinished);
}

HuggingFaceAIClient::~HuggingFaceAIClient() {
}

void HuggingFaceAIClient::setApiKey(const QString &apiKey) {
    m_apiKey = apiKey;
}

void HuggingFaceAIClient::setModel(const QString &model) {
    if (!model.isEmpty()) {
        m_model = model;
    }
}

void HuggingFaceAIClient::setTavilyApiKey(const QString &tavilyKey) {
    if (m_searchManager) {
        m_searchManager->setTavilyApiKey(tavilyKey);
    }
}

ProviderStatus HuggingFaceAIClient::defaultStatus() const {
    ProviderStatus s;
    s.provider = "huggingface";
    s.rpmMax = 60;
    s.tpmMax = 100000;
    return s;
}

void HuggingFaceAIClient::sendRequest(const QString &prompt, const QList<QPair<QString, QString>> &history, const QString &sessionContext, const QString &systemInstruction) {
    if (m_apiKey.isEmpty()) {
        emit requestFinished("HuggingFace APIキーが設定されていません。local_settings.json を確認してください。", false);
        return;
    }

    m_isToolCalling = false;
    m_pendingPrompt = prompt;

    QString modelName = m_model.isEmpty() ? "meta-llama/Llama-3.1-8B-Instruct" : m_model;
    QString urlStr = QString("https://api-inference.huggingface.co/models/%1/v1/chat/completions").arg(modelName);
    QUrl url(urlStr);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(m_apiKey).toUtf8());

    QJsonObject requestBody;
    requestBody["model"] = modelName;

    QJsonArray messages;
    QJsonObject systemMessage;
    systemMessage["role"] = "system";

    QString avatarName = "AIアシスタント";
    AIClientManager *manager = qobject_cast<AIClientManager*>(parent());
    if (manager) {
        avatarName = manager->avatarName();
    }

    QString systemPrompt = QString("あなたはデスクトップマスコットのキャラクター「%1」です。自己紹介や名前を聞かれた際は、必ず「%1」と名乗ってください。自分自身を「AIアシスタント」や「AI」といった一般名詞で呼ばず、必ずキャラクター名「%1」または「私」と名乗ってください。それ以外の名前を使用しないでください。フレンドリーで短い日本語で回答してください。ユーザーの入力を回答で反復しないでください。ユーザーの質問に対して独立した回答を生成してください。")
                            .arg(avatarName);

    if (!systemInstruction.isEmpty()) {
        systemPrompt += "\n" + systemInstruction;
    }
    if (!sessionContext.isEmpty()) {
        systemPrompt += "\n" + sessionContext;
    }

    systemMessage["content"] = systemPrompt;
    messages.append(systemMessage);

    for (const auto &pair : history) {
        QJsonObject userMsg;
        userMsg["role"] = "user";
        userMsg["content"] = pair.first;
        messages.append(userMsg);

        QJsonObject modelMsg;
        modelMsg["role"] = "assistant";
        modelMsg["content"] = pair.second;
        messages.append(modelMsg);
    }

    QJsonObject currentMessage;
    currentMessage["role"] = "user";
    currentMessage["content"] = prompt;
    messages.append(currentMessage);

    requestBody["messages"] = messages;

    QJsonDocument doc(requestBody);
    m_networkManager->post(request, doc.toJson());
}

void HuggingFaceAIClient::on_networkReplyFinished(QNetworkReply *reply) {
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString errStr = reply->errorString();
        QByteArray errBody = reply->readAll();
        qWarning() << "HuggingFace API Request Failed. Code:" << httpCode << "Error:" << errStr << "Body:" << errBody;

        QString detailedErr = errStr;
        QJsonDocument errDoc = QJsonDocument::fromJson(errBody);
        if (errDoc.isObject() && errDoc.object().contains("error")) {
            detailedErr = errDoc.object().value("error").toString();
        } else if (!errBody.isEmpty()) {
            detailedErr = QString::fromUtf8(errBody).left(120);
        }

        emit requestFinished(QString("HuggingFace API エラー (%1): %2").arg(httpCode).arg(detailedErr), false);
        return;
    }

    QByteArray responseData = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    if (!doc.isObject()) {
        emit requestFinished("HuggingFace 無効なJSONレスポンス形式", false);
        return;
    }

    QJsonObject obj = doc.object();
    if (!obj.contains("choices")) {
        emit requestFinished("HuggingFace レスポンスにchoicesが含まれていません", false);
        return;
    }

    QJsonArray choices = obj["choices"].toArray();
    if (choices.isEmpty()) {
        emit requestFinished("HuggingFace 空のchoicesレスポンス", false);
        return;
    }

    QJsonObject firstChoice = choices.first().toObject();
    QJsonObject messageObj = firstChoice["message"].toObject();
    QString replyText = messageObj["content"].toString();

    emit requestFinished(replyText.trimmed(), true);
}

void HuggingFaceAIClient::on_searchFinished(const QString &resultText, bool success) {
    Q_UNUSED(resultText);
    Q_UNUSED(success);
}
