#include "openrouter_ai_client.h"
#include "ai_client_manager.h"
#include "../search/search_manager.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QThread>
#include <QDebug>

OpenRouterAIClient::OpenRouterAIClient(QObject *parent)
    : IAIClient(parent), m_isToolCalling(false), m_model("meta-llama/llama-3.1-8b-instruct:free")
{
    m_networkManager = new QNetworkAccessManager(this);
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &OpenRouterAIClient::on_networkReplyFinished);

    m_searchManager = new SearchManager(this);
    connect(m_searchManager, &SearchManager::searchFinished,
            this, &OpenRouterAIClient::on_searchFinished);
}

OpenRouterAIClient::~OpenRouterAIClient() {
}

void OpenRouterAIClient::setApiKey(const QString &apiKey) {
    m_apiKey = apiKey;
    if (!m_apiKey.isEmpty()) {
        fetchAvailableModels();
    }
}

void OpenRouterAIClient::fetchAvailableModels() {
    if (thread() != QThread::currentThread()) {
        QMetaObject::invokeMethod(this, "fetchAvailableModels", Qt::QueuedConnection);
        return;
    }
    if (m_apiKey.isEmpty() || m_isFetchingModels) return;
    m_isFetchingModels = true;

    QUrl url("https://openrouter.ai/api/v1/models");
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QString("Bearer %1").arg(m_apiKey).toUtf8());
    m_networkManager->get(request);
}

void OpenRouterAIClient::setModel(const QString &model) {
    if (!model.isEmpty()) {
        m_model = model;
    }
}

void OpenRouterAIClient::setTavilyApiKey(const QString &tavilyKey) {
    if (m_searchManager) {
        m_searchManager->setTavilyApiKey(tavilyKey);
    }
}

ProviderStatus OpenRouterAIClient::defaultStatus() const {
    ProviderStatus s;
    s.provider = "openrouter";
    s.rpmMax = 60;
    s.tpmMax = 100000;
    return s;
}

void OpenRouterAIClient::sendRequest(const QString &prompt, const QList<QPair<QString, QString>> &history, const QString &sessionContext, const QString &systemInstruction) {
    if (m_apiKey.isEmpty()) {
        emit requestFinished("OpenRouter APIキーが設定されていません。local_settings.json を確認してください。", false, 0);
        return;
    }

    m_isToolCalling = false;
    m_pendingPrompt = prompt;
    m_pendingHistory = history;
    m_pendingSessionContext = sessionContext;
    m_pendingSystemInstruction = systemInstruction;

    QString targetModel = m_model.trimmed().isEmpty() ? "meta-llama/llama-3.1-8b-instruct:free" : m_model.trimmed();

    QUrl url("https://openrouter.ai/api/v1/chat/completions");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(m_apiKey).toUtf8());
    request.setRawHeader("HTTP-Referer", "https://github.com/BLUE000/AiAssistantAvatar");
    request.setRawHeader("X-Title", "AiAssistantAvatar");

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

    QString systemPrompt = buildBaseSystemPrompt(avatarName);



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

void OpenRouterAIClient::on_networkReplyFinished(QNetworkReply *reply) {
    AIClientManager *manager = qobject_cast<AIClientManager*>(parent());
    if (manager) {
        manager->tracker().updateFromReply(QStringLiteral("openrouter"), reply);
    }
    reply->deleteLater();


    if (reply->request().url().path().endsWith("/models")) {
        m_isFetchingModels = false;
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (doc.isObject() && doc.object().contains("data")) {
                QJsonArray modelsArray = doc.object().value("data").toArray();
                QString chosenModel;
                for (const QJsonValue &val : modelsArray) {
                    QJsonObject mObj = val.toObject();
                    QString id = mObj.value("id").toString();
                    if (id.endsWith(":free")) {
                        chosenModel = id;
                        // llama-3.1 系の free モデルを最優先
                        if (id.contains("llama-3.1-8b") || id.contains("llama-3-8b")) {
                            chosenModel = id;
                            break;
                        }
                    }
                }
                if (!chosenModel.isEmpty()) {
                    m_model = chosenModel;
                    qDebug() << "[OpenRouterAIClient] Auto-selected active free model:" << m_model;
                }
            }
        }
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString errStr = reply->errorString();
        QByteArray errBody = reply->readAll();
        qWarning() << "[OpenRouterAIClient] HTTP Error" << httpCode << errStr << "Body:" << errBody;

        // 404 (モデル廃止/NotFound) かつ未リトライの場合、フォールバックモデルへ切り替えて自動リトライ
        if (httpCode == 404 && !m_hasRetried404) {
            m_hasRetried404 = true;
            qDebug() << "OpenRouterAIClient: 404 received for model" << m_model << "- auto-fallbacking and retrying...";
            m_model = (m_model == "meta-llama/llama-3.1-8b-instruct:free") ? "google/gemma-2-9b-it:free" : "meta-llama/llama-3.1-8b-instruct:free";
            sendRequest(m_pendingPrompt, m_pendingHistory, m_pendingSessionContext, m_pendingSystemInstruction);
            return;
        }

        QString emitText;
        QJsonDocument errDoc = QJsonDocument::fromJson(errBody);
        if (!errBody.isEmpty() && errDoc.isObject()) {
            emitText = QString::fromUtf8(errBody);
        } else {
            emitText = QString("OpenRouter API エラー (%1): %2").arg(httpCode).arg(errStr);
        }

        emit requestFinished(emitText, false, httpCode);
        return;
    }

    m_hasRetried404 = false;

    QByteArray responseData = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    if (!doc.isObject()) {
        emit requestFinished("OpenRouter 無効なJSONレスポンス形式", false, 0);
        return;
    }

    QJsonObject obj = doc.object();
    if (!obj.contains("choices")) {
        emit requestFinished("OpenRouter レスポンスにchoicesが含まれていません", false, 0);
        return;
    }

    QJsonArray choices = obj["choices"].toArray();
    if (choices.isEmpty()) {
        emit requestFinished("OpenRouter 空のchoicesレスポンス", false, 0);
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

void OpenRouterAIClient::on_searchFinished(const QString &resultText, bool success) {
    if (!m_isToolCalling) return;
    m_isToolCalling = false;

    if (!success || resultText.isEmpty()) {
        emit requestFinished("すみません、現在の天気情報を取得できません。", true, 200);
        return;
    }

    QString targetModel = m_model.isEmpty() ? "meta-llama/llama-3.1-8b-instruct:free" : m_model;
    QUrl url("https://openrouter.ai/api/v1/chat/completions");
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
