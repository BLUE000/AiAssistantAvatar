#include "mistral_ai_client.h"
#include "../search/search_manager.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QDebug>

MistralAIClient::MistralAIClient(QObject *parent)
    : IAIClient(parent), m_isToolCalling(false)
{
    m_networkManager = new QNetworkAccessManager(this);
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &MistralAIClient::on_networkReplyFinished);

    m_searchManager = new SearchManager(this);
    connect(m_searchManager, &SearchManager::searchFinished,
            this, &MistralAIClient::on_searchFinished);
}

MistralAIClient::~MistralAIClient() {
}

void MistralAIClient::setApiKey(const QString &apiKey) {
    m_apiKey = apiKey;
}

void MistralAIClient::setTavilyApiKey(const QString &tavilyKey) {
    if (m_searchManager) {
        m_searchManager->setTavilyApiKey(tavilyKey);
    }
}

void MistralAIClient::sendRequest(const QString &prompt, const QList<QPair<QString, QString>> &history, const QString &sessionContext) {
    if (m_apiKey.isEmpty()) {
        emit requestFinished("Mistral APIキーが設定されていません。local_settings.json を確認してください。", false);
        return;
    }

    m_isToolCalling = false;
    m_pendingPrompt = prompt;

    QUrl url("https://api.mistral.ai/v1/chat/completions");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(m_apiKey).toUtf8());

    QJsonObject requestBody;
    requestBody["model"] = "open-mistral-7b"; // デフォルトの軽量モデル

    QJsonArray messages;
    QJsonObject systemMessage;
    systemMessage["role"] = "system";

    QString systemPrompt = "あなたはデスクトップマスコットのAIアシスタントです。フレンドリーで短い日本語で回答してください。ユーザーの入力を回答で反復しないでください。ユーザーの質問に対して独立した回答を生成してください。";
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

    // 状態退避
    m_pendingMessages = messages;

    requestBody["messages"] = messages;

    // tools (Function Calling) の追加
    QJsonObject tool;
    tool["type"] = "function";
    QJsonObject functionObj;
    functionObj["name"] = "web_search";
    functionObj["description"] = "Perform a web search to fetch latest information or query search engines for current events.";
    
    QJsonObject parameters;
    parameters["type"] = "object";
    QJsonObject properties;
    QJsonObject queryProp;
    queryProp["type"] = "string";
    queryProp["description"] = "The search query to retrieve information for.";
    properties["query"] = queryProp;
    parameters["properties"] = properties;
    
    QJsonArray requiredArray;
    requiredArray.append("query");
    parameters["required"] = requiredArray;
    
    functionObj["parameters"] = parameters;
    tool["function"] = functionObj;

    QJsonArray toolsArray;
    toolsArray.append(tool);
    requestBody["tools"] = toolsArray;
    requestBody["tool_choice"] = "auto";

    QJsonDocument doc(requestBody);
    QByteArray postData = doc.toJson();

    qDebug() << "MistralAIClient: Sending request with tools, history size:" << history.size();
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

                // tool_calls が含まれているかチェック
                if (messageObj.contains("tool_calls") && messageObj["tool_calls"].isArray()) {
                    QJsonArray toolCalls = messageObj["tool_calls"].toArray();
                    if (!toolCalls.isEmpty()) {
                        QJsonObject toolCall = toolCalls.at(0).toObject();
                        QString funcName = toolCall["function"].toObject()["name"].toString();
                        if (funcName == "web_search") {
                            m_activeToolCallId = toolCall["id"].toString();
                            QString argsStr = toolCall["function"].toObject()["arguments"].toString();
                            
                            // 引数のパース
                            QJsonDocument argsDoc = QJsonDocument::fromJson(argsStr.toUtf8());
                            QString query = argsDoc.object()["query"].toString();

                            qDebug() << "MistralAIClient: Function call detected. id:" << m_activeToolCallId << "query:" << query;
                            
                            // tool_calls を含む assistant メッセージを履歴に追加する (Mistral APIの仕様上必須)
                            m_pendingMessages.append(messageObj);

                            m_isToolCalling = true;
                            m_searchManager->executeSearch(query);
                            return;
                        }
                    }
                }

                // 通常のテキスト応答
                QString replyText = messageObj["content"].toString();
                emit requestFinished(replyText.trimmed(), true);
                return;
            }
        }
    }

    emit requestFinished("レスポンスから適切なメッセージが見つかりませんでした。", false);
}

void MistralAIClient::on_searchFinished(const QString &resultText, bool success) {
    qDebug() << "MistralAIClient: Search finished. Success:" << success << "Result length:" << resultText.length();

    // 検索結果 (toolロール) をメッセージ履歴に追加
    QJsonObject toolResponse;
    toolResponse["role"] = "tool";
    toolResponse["name"] = "web_search";
    toolResponse["tool_call_id"] = m_activeToolCallId;
    toolResponse["content"] = resultText;
    m_pendingMessages.append(toolResponse);

    // 再度 Mistral に最終回答リクエストを送信
    QUrl url("https://api.mistral.ai/v1/chat/completions");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(m_apiKey).toUtf8());

    QJsonObject requestBody;
    requestBody["model"] = "open-mistral-7b";
    requestBody["messages"] = m_pendingMessages; // 検索結果を含んだメッセージ履歴

    // 再送信時には tools は指定しない (最終テキスト生成モードにするため)

    QJsonDocument doc(requestBody);
    QByteArray postData = doc.toJson();

    qDebug() << "MistralAIClient: Sending final response request to Mistral...";
    m_networkManager->post(request, postData);
}
