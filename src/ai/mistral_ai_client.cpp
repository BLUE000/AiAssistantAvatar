#include "mistral_ai_client.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QDebug>

MistralAIClient::MistralAIClient(QObject *parent)
    : IAIClient(parent) 
{
    m_networkManager = new QNetworkAccessManager(this);
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &MistralAIClient::on_networkReplyFinished);
}

MistralAIClient::~MistralAIClient() {
}

void MistralAIClient::setApiKey(const QString &apiKey) {
    m_apiKey = apiKey;
}

void MistralAIClient::sendRequest(const QString &prompt, const QList<QPair<QString, QString>> &history, const QString &sessionContext) {
    if (m_apiKey.isEmpty()) {
        emit requestFinished("Mistral APIキーが設定されていません。local_settings.json を確認してください。", false);
        return;
    }

    QUrl url("https://api.mistral.ai/v1/chat/completions");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(m_apiKey).toUtf8());

    // JSONリクエストボディの構築
    QJsonObject requestBody;
    requestBody["model"] = "open-mistral-7b"; // デフォルトの軽量モデル
    
    QJsonArray messages;
    QJsonObject systemMessage;
    systemMessage["role"] = "system";
    
    QString systemPrompt = "あなたはデスクトップマスコットのAIアシスタントです。フレンドリーで短い日本語で回答してください。";
    if (!sessionContext.isEmpty()) {
        systemPrompt += "\n\n以下のマークダウンは以前の会話のコンテキスト（要約や前提知識）です。これに基づいて応答してください:\n" + sessionContext;
    }
    systemMessage["content"] = systemPrompt;
    messages.append(systemMessage);

    // 過去の対話履歴を追加
    for (const auto &pair : history) {
        QJsonObject histUser;
        histUser["role"] = "user";
        histUser["content"] = pair.first;
        messages.append(histUser);

        QJsonObject histAssistant;
        histAssistant["role"] = "assistant";
        histAssistant["content"] = pair.second;
        messages.append(histAssistant);
    }

    // 最新のプロンプトを追加
    QJsonObject userMessage;
    userMessage["role"] = "user";
    userMessage["content"] = prompt;
    messages.append(userMessage);

    requestBody["messages"] = messages;

    QJsonDocument doc(requestBody);
    QByteArray postData = doc.toJson();

    qDebug() << "MistralAIClient: Sending POST request to Mistral API with" << history.size() << "history messages...";
    m_networkManager->post(request, postData);
}

void MistralAIClient::on_networkReplyFinished(QNetworkReply *reply) {
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        QString errorMsg = QString("ネットワークエラー: %1 (%2)")
                            .arg(reply->errorString())
                            .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
        qWarning() << "MistralAIClient Error:" << errorMsg;
        emit requestFinished(errorMsg, false);
        return;
    }

    QByteArray responseData = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    if (doc.isNull() || !doc.isObject()) {
        emit requestFinished("レスポンスJSONの解析に失敗しました。", false);
        return;
    }

    QJsonObject obj = doc.object();
    if (obj.contains("choices") && obj["choices"].isArray()) {
        QJsonArray choices = obj["choices"].toArray();
        if (!choices.isEmpty()) {
            QJsonObject firstChoice = choices.at(0).toObject();
            if (firstChoice.contains("message") && firstChoice["message"].isObject()) {
                QJsonObject messageObj = firstChoice["message"].toObject();
                QString replyText = messageObj["content"].toString();
                emit requestFinished(replyText.trimmed(), true);
                return;
            }
        }
    }

    emit requestFinished("レスポンスから適切なメッセージが見つかりませんでした。", false);
}
