#include "sakura_ai_client.h"
#include "ai_client_manager.h"
#include "../search/search_manager.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QDebug>

SakuraAIClient::SakuraAIClient(QObject *parent)
    : IAIClient(parent), m_isToolCalling(false), m_model("llm-jp-3.1-8x13b-instruct4")
{
    m_networkManager = new QNetworkAccessManager(this);
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &SakuraAIClient::on_networkReplyFinished);

    m_searchManager = new SearchManager(this);
    connect(m_searchManager, &SearchManager::searchFinished,
            this, &SakuraAIClient::on_searchFinished);
}

SakuraAIClient::~SakuraAIClient() {
}

void SakuraAIClient::setApiKey(const QString &apiKey) {
    m_apiKey = apiKey;
}

void SakuraAIClient::setModel(const QString &model) {
    if (!model.isEmpty()) {
        m_model = model;
    }
}

void SakuraAIClient::setTavilyApiKey(const QString &tavilyKey) {
    if (m_searchManager) {
        m_searchManager->setTavilyApiKey(tavilyKey);
    }
}

ProviderStatus SakuraAIClient::defaultStatus() const {
    ProviderStatus s;
    s.provider = "sakura";
    s.rpmMax = 60;
    s.tpmMax = 100000;
    return s;
}

void SakuraAIClient::sendRequest(const QString &prompt, const QList<QPair<QString, QString>> &history, const QString &sessionContext, const QString &systemInstruction) {
    if (m_apiKey.isEmpty()) {
        emit requestFinished("さくらAI APIキーが設定されていません。local_settings.json を確認してください。", false, 0);
        return;
    }

    m_isToolCalling = false;
    m_pendingPrompt = prompt;

    QUrl url("https://api.ai.sakura.ad.jp/v1/chat/completions");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(m_apiKey).toUtf8());

    QString targetModel = m_model.isEmpty() ? "llm-jp-3.1-8x13b-instruct4" : m_model;
    QJsonObject requestBody;
    requestBody["model"] = targetModel;

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
        userMsg["content"] = cleanHistoryPrompt(pair.first);
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

    m_pendingMessages = messages;
    requestBody["messages"] = messages;
    requestBody["max_tokens"] = 1024;

    // tools (Function Calling / Web Search)
    QJsonObject tool;
    tool["type"] = "function";
    QJsonObject functionObj;
    functionObj["name"] = "web_search";
    functionObj["description"] = "天気、最新ニュース、リアルタイム情報を取得するために使用します。";
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

    QJsonArray tools;
    tools.append(tool);
    requestBody["tools"] = tools;

    QJsonDocument doc(requestBody);
    m_networkManager->post(request, doc.toJson());
}

void SakuraAIClient::on_networkReplyFinished(QNetworkReply *reply) {
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString errStr = reply->errorString();
        QByteArray errBody = reply->readAll();
        qWarning() << "さくらAI API Request Failed. Code:" << httpCode << "Error:" << errStr << "Body:" << errBody;

        QString detailMessage = errStr;
        if (!errBody.isEmpty()) {
            QJsonDocument errDoc = QJsonDocument::fromJson(errBody);
            if (errDoc.isObject()) {
                QJsonObject errObj = errDoc.object();
                if (errObj.contains("error")) {
                    QJsonValue errVal = errObj["error"];
                    if (errVal.isObject() && errVal.toObject().contains("message")) {
                        detailMessage = errVal.toObject()["message"].toString();
                    } else if (errVal.isString()) {
                        detailMessage = errVal.toString();
                    }
                } else if (errObj.contains("message")) {
                    detailMessage = errObj["message"].toString();
                }
            } else {
                detailMessage += " (" + QString::fromUtf8(errBody).trimmed() + ")";
            }
        }

        if (httpCode == 429) {
            qWarning() << "さくらAI API Rate Limit Exceeded (HTTP 429)";
        }

        emit requestFinished(QString("さくらAI API エラー (%1): %2").arg(httpCode).arg(detailMessage), false, httpCode);
        return;
    }

    QByteArray responseData = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    if (!doc.isObject()) {
        emit requestFinished("さくらAI 無効なJSONレスポンス形式", false, 0);
        return;
    }

    QJsonObject obj = doc.object();
    if (!obj.contains("choices")) {
        emit requestFinished("さくらAI レスポンスにchoicesが含まれていません", false, 0);
        return;
    }

    QJsonArray choices = obj["choices"].toArray();
    if (choices.isEmpty()) {
        emit requestFinished("さくらAI 空のchoicesレスポンス", false, 0);
        return;
    }

    QJsonObject firstChoice = choices.first().toObject();
    QJsonObject messageObj = firstChoice["message"].toObject();

    if (messageObj.contains("tool_calls")) {
        QJsonArray toolCalls = messageObj["tool_calls"].toArray();
        if (!toolCalls.isEmpty()) {
            QJsonObject callObj = toolCalls.first().toObject();
            m_activeToolCallId = callObj.value("id").toString();
            QJsonObject funcObj = callObj.value("function").toObject();
            QString funcName = funcObj.value("name").toString();

            if (funcName == "web_search" && m_searchManager) {
                QString argsStr = funcObj.value("arguments").toString();
                QJsonDocument argsDoc = QJsonDocument::fromJson(argsStr.toUtf8());
                QString query = argsDoc.object().value("query").toString();
                if (query.isEmpty()) query = m_pendingPrompt;

                m_isToolCalling = true;
                m_searchManager->executeSearch(query);
                return;
            }
        }
    }

    QString replyText = messageObj["content"].toString();
    emit requestFinished(replyText.trimmed(), true, 200);
}

void SakuraAIClient::on_searchFinished(const QString &resultText, bool success) {
    if (!m_isToolCalling) return;
    m_isToolCalling = false;

    if (!success || resultText.isEmpty()) {
        emit requestFinished("すみません、現在の天気情報を取得できません。", true, 200);
        return;
    }

    QString targetModel = m_model.isEmpty() ? "llm-jp-3.1-8x13b-instruct4" : m_model;
    QUrl url("https://api.ai.sakura.ad.jp/v1/chat/completions");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(m_apiKey).toUtf8());

    QJsonObject requestBody;
    requestBody["model"] = targetModel;

    QJsonArray messages = m_pendingMessages;

    QJsonObject assistantToolMsg;
    assistantToolMsg["role"] = "assistant";
    QJsonArray toolCalls;
    QJsonObject tc;
    tc["id"] = m_activeToolCallId;
    tc["type"] = "function";
    QJsonObject func;
    func["name"] = "web_search";
    func["arguments"] = QString("{\"query\":\"%1\"}").arg(m_pendingPrompt);
    tc["function"] = func;
    toolCalls.append(tc);
    assistantToolMsg["tool_calls"] = toolCalls;
    messages.append(assistantToolMsg);

    QJsonObject toolMsg;
    toolMsg["role"] = "tool";
    toolMsg["tool_call_id"] = m_activeToolCallId;
    toolMsg["name"] = "web_search";
    toolMsg["content"] = resultText;
    messages.append(toolMsg);

    requestBody["messages"] = messages;
    requestBody["max_tokens"] = 1024;

    QJsonDocument doc(requestBody);
    m_networkManager->post(request, doc.toJson());
}
