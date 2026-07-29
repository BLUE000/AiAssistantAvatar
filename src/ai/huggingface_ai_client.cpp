#include "huggingface_ai_client.h"
#include "ai_client_manager.h"
#include "../search/search_manager.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QThread>
#include <QDebug>

HuggingFaceAIClient::HuggingFaceAIClient(QObject *parent)
    : IAIClient(parent), m_isToolCalling(false), m_model("Qwen/Qwen2.5-Coder-32B-Instruct")
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
    if (!m_apiKey.isEmpty()) {
        fetchAvailableModels();
    }
}

void HuggingFaceAIClient::fetchAvailableModels() {
    if (m_apiKey.isEmpty() || m_isFetchingModels) return;
    m_isFetchingModels = true;

    QUrl url("https://router.huggingface.co/v1/models");
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QString("Bearer %1").arg(m_apiKey).toUtf8());
    m_networkManager->get(request);
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
        emit requestFinished("HuggingFace APIキーが設定されていません。local_settings.json を確認してください。", false, 0);
        return;
    }

    m_isToolCalling = false;
    m_pendingPrompt = prompt;

    QString modelName = m_model.isEmpty() ? "Qwen/Qwen2.5-Coder-32B-Instruct" : m_model;
    QString urlStr = "https://router.huggingface.co/v1/chat/completions";
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

    QString systemPrompt = QString("あなたはデスクトップマスコットのキャラクター「%1」です。自己紹介や名前を聞かれた際は、必ず「%1」と名乗ってください。自分自身を「AIアシスタント」や「AI」といった一般名詞で呼ばず、必ずキャラクター名「%1」または「私」と名乗ってください。それ以外の名前を使用しないでください。フレンドリーで短い日本語で回答してください。ユーザーの入力を回答で反復しないでください。ユーザーの質問に対して独立した回答を生成してください。対話相手のお名前や呼び名が明示的に指定されていない場合は、相手のお名前を推測・捏造せず、お名前を呼ばずにそのまま回答してください。")
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
    requestBody["stream"] = false;

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

void HuggingFaceAIClient::on_networkReplyFinished(QNetworkReply *reply) {
    reply->deleteLater();

    if (m_isFetchingModels && reply->url().toString().contains("/models")) {
        m_isFetchingModels = false;
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (doc.isObject() && doc.object().contains("data")) {
                QJsonArray arr = doc.object()["data"].toArray();
                for (const QJsonValue &val : arr) {
                    if (val.isObject()) {
                        QString id = val.toObject().value("id").toString();
                        if (id.contains("Qwen2.5-Coder", Qt::CaseInsensitive) ||
                            id.contains("Qwen2.5-72B", Qt::CaseInsensitive) ||
                            id.contains("Qwen2.5-7B", Qt::CaseInsensitive) ||
                            id.contains("Mistral-7B", Qt::CaseInsensitive)) {
                            m_model = id;
                            qDebug() << "HuggingFaceAIClient: Automatically selected model:" << m_model;
                            break;
                        }
                    }
                }
            }
        }
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString errStr = reply->errorString();
        QByteArray errBody = reply->readAll();
        qWarning() << "HuggingFace API Request Failed. Code:" << httpCode << "Error:" << errStr << "Body:" << errBody;

        QString detailedErr;
        if (!errBody.isEmpty()) {
            QJsonDocument errDoc = QJsonDocument::fromJson(errBody);
            if (errDoc.isObject()) {
                QJsonObject errObj = errDoc.object();
                if (errObj.contains("detail")) {
                    QJsonValue detVal = errObj["detail"];
                    if (detVal.isString()) {
                        detailedErr = detVal.toString();
                    } else if (detVal.isArray()) {
                        QJsonArray detArr = detVal.toArray();
                        if (!detArr.isEmpty() && detArr.first().isObject()) {
                            detailedErr = detArr.first().toObject().value("msg").toString();
                        }
                    }
                }
                if (detailedErr.isEmpty() && errObj.contains("error")) {
                    QJsonValue errVal = errObj["error"];
                    if (errVal.isObject() && errVal.toObject().contains("message")) {
                        detailedErr = errVal.toObject()["message"].toString();
                    } else if (errVal.isString()) {
                        detailedErr = errVal.toString();
                    }
                }
                if (detailedErr.isEmpty() && errObj.contains("message")) {
                    detailedErr = errObj["message"].toString();
                }
            } else {
                detailedErr = QString::fromUtf8(errBody).trimmed();
            }
        }

        if (detailedErr.isEmpty()) {
            detailedErr = errStr;
        }

        emit requestFinished(QString("HuggingFace API エラー (%1): %2").arg(httpCode).arg(detailedErr), false, httpCode);
        return;
    }

    QByteArray responseData = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    
    QString replyText;

    if (doc.isObject()) {
        QJsonObject obj = doc.object();
        if (obj.contains("choices")) {
            QJsonArray choices = obj["choices"].toArray();
            if (!choices.isEmpty()) {
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

                replyText = messageObj["content"].toString();
            }
        } else if (obj.contains("generated_text")) {
            replyText = obj.value("generated_text").toString();
        }
    } else if (doc.isArray()) {
        QJsonArray arr = doc.array();
        if (!arr.isEmpty() && arr.first().isObject()) {
            QJsonObject item = arr.first().toObject();
            if (item.contains("generated_text")) {
                replyText = item.value("generated_text").toString();
            }
        }
    }

    if (replyText.isEmpty()) {
        emit requestFinished("HuggingFace 応答テキストの解析に失敗しました。", false, 0);
        return;
    }

    emit requestFinished(replyText.trimmed(), true, 200);
}

void HuggingFaceAIClient::on_searchFinished(const QString &resultText, bool success) {
    if (!m_isToolCalling) return;
    m_isToolCalling = false;

    if (!success || resultText.isEmpty()) {
        emit requestFinished("すみません、現在の天気情報を取得できません。", true, 200);
        return;
    }

    QString modelName = m_model.isEmpty() ? "Qwen/Qwen2.5-Coder-32B-Instruct" : m_model;
    QUrl url("https://router.huggingface.co/v1/chat/completions");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(m_apiKey).toUtf8());

    QJsonObject requestBody;
    requestBody["model"] = modelName;

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
    requestBody["stream"] = false;

    QJsonDocument doc(requestBody);
    m_networkManager->post(request, doc.toJson());
}
