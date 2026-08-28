#include "gemini_ai_client.h"
#include "../search/search_manager.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QThread>
#include <QDebug>

GeminiAIClient::GeminiAIClient(QObject *parent)
    : IAIClient(parent), m_isToolCalling(false), m_model("gemini-2.0-flash")
{
    m_networkManager = new QNetworkAccessManager(this);
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &GeminiAIClient::on_networkReplyFinished);

    m_searchManager = new SearchManager(this);
    connect(m_searchManager, &SearchManager::searchFinished,
            this, &GeminiAIClient::on_searchFinished);
}

GeminiAIClient::~GeminiAIClient() {
}

void GeminiAIClient::setApiKey(const QString &apiKey) {
    m_apiKey = apiKey;
}

void GeminiAIClient::setModel(const QString &model) {
    if (!model.isEmpty()) {
        m_model = model;
    }
}

void GeminiAIClient::setTavilyApiKey(const QString &tavilyKey) {
    if (m_searchManager) {
        m_searchManager->setTavilyApiKey(tavilyKey);
    }
}

ProviderStatus GeminiAIClient::defaultStatus() const {
    ProviderStatus s;
    s.provider = "gemini";
    s.rpmMax = 15;
    s.rpmRemaining = 15;
    s.rpdMax = 1500;
    s.rpdRemaining = 1500;
    s.tpmMax = 1000000;
    s.tpmRemaining = 1000000;
    s.contextWindow = 1048576;
    s.toolCall = true;
    s.supportsDiff = false;
    s.cost = 0.0;
    return s;
}

void GeminiAIClient::sendRequest(const QString &prompt, const QList<QPair<QString, QString>> &history, const QString &sessionContext, const QString &systemInstruction) {
    if (m_apiKey.isEmpty()) {
        emit requestFinished("Gemini APIキーが設定されていません。local_settings.json または AI設定を確認してください。", false, 0);
        return;
    }

    m_isToolCalling = false;
    m_pendingPrompt = prompt;

    QUrl url("https://generativelanguage.googleapis.com/v1beta/openai/chat/completions");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(m_apiKey).toUtf8());

    QJsonObject requestBody;
    requestBody["model"] = m_model;

    QJsonArray messages;
    QJsonObject systemMessage;
    systemMessage["role"] = "system";

    QString avatarName = "AIアシスタント";
    if (parent()) {
        QMetaObject::invokeMethod(parent(), "avatarName",
                                  Qt::DirectConnection,
                                  Q_RETURN_ARG(QString, avatarName));
        if (avatarName.isEmpty()) {
            avatarName = "AIアシスタント";
        }
    }

    QString systemPrompt = buildBaseSystemPrompt(avatarName)
                           + "ユーザー自身が「〇〇です」「〇〇だよ」と名乗る自己紹介や、「〇〇と呼んで」などの呼び名指定をした場合のみ、『update_nickname』ツールを呼び出してニックネームを設定してください。文脈中に第三者や配信者の名前が登場しただけの場合（例：「〇〇さんにおすすめの〜」など）は、ニックネーム設定と誤認しないでください。"
                             "また、ユーザーから天気、為替レート、最新ニュース、リアルタイム情報、またはあなたが最新の正確な知識を持っていない事柄について質問された場合は、自身の過去の知識で回答しようとせず、必ず『web_search』ツールを呼び出して最新情報を検索してください。"
                             "また、あなたには翻訳機能があります。ユーザーが翻訳をしたい場合、チャット欄で「[ウェイクワード] trans [言語] [翻訳したいテキスト]」と入力すれば翻訳を実行できます（例：「!ai trans en こんにちは」）。[言語]を省略した場合はデフォルトで日本語に翻訳されます。ユーザーから翻訳の使い方を聞かれた場合は、この「[ウェイクワード] trans [言語] [テキスト]」というコマンドの使い方を親切に教えてあげてください。";



    if (!systemInstruction.isEmpty()) {
        systemPrompt = systemInstruction;
    }

    if (!sessionContext.isEmpty()) {
        systemPrompt += "\n\n【直近の会話・配信コンテキスト】\n" + sessionContext;
    }

    systemMessage["content"] = systemPrompt;
    messages.append(systemMessage);

    for (const auto &pair : history) {
        QJsonObject userMsg;
        userMsg["role"] = "user";
        userMsg["content"] = pair.first;
        messages.append(userMsg);

        QJsonObject assistantMsg;
        assistantMsg["role"] = "assistant";
        assistantMsg["content"] = pair.second;
        messages.append(assistantMsg);
    }

    QJsonObject currentMessage;
    currentMessage["role"] = "user";
    currentMessage["content"] = prompt;
    messages.append(currentMessage);

    requestBody["messages"] = messages;
    m_pendingMessages = messages;

    // tools (Function Calling) 定義
    QJsonArray tools;

    QJsonObject webSearchFunc;
    webSearchFunc["name"] = "web_search";
    webSearchFunc["description"] = "最新のWeb情報やリアルタイムデータ（天気、ニュース、株価、トレンドなど）を検索します。";
    QJsonObject webSearchProps;
    QJsonObject queryProp;
    queryProp["type"] = "string";
    queryProp["description"] = "検索キーワード";
    webSearchProps["query"] = queryProp;
    QJsonObject webSearchParams;
    webSearchParams["type"] = "object";
    webSearchParams["properties"] = webSearchProps;
    QJsonArray reqWebSearch;
    reqWebSearch.append("query");
    webSearchParams["required"] = reqWebSearch;
    webSearchFunc["parameters"] = webSearchParams;

    QJsonObject toolWebSearch;
    toolWebSearch["type"] = "function";
    toolWebSearch["function"] = webSearchFunc;
    tools.append(toolWebSearch);

    QJsonObject nicknameFunc;
    nicknameFunc["name"] = "update_nickname";
    nicknameFunc["description"] = "対話相手（ユーザー）の呼び名（ニックネーム）を更新・設定します。ユーザーが「〇〇と呼んで」「私の名前は〇〇です」と名乗った時のみ使用します。";
    QJsonObject nicknameProps;
    QJsonObject nicknameProp;
    nicknameProp["type"] = "string";
    nicknameProp["description"] = "設定する呼び名（ニックネーム）";
    nicknameProps["nickname"] = nicknameProp;
    QJsonObject nicknameParams;
    nicknameParams["type"] = "object";
    nicknameParams["properties"] = nicknameProps;
    QJsonArray reqNickname;
    reqNickname.append("nickname");
    nicknameParams["required"] = reqNickname;
    nicknameFunc["parameters"] = nicknameParams;

    QJsonObject toolNickname;
    toolNickname["type"] = "function";
    toolNickname["function"] = nicknameFunc;
    tools.append(toolNickname);

    requestBody["tools"] = tools;
    m_toolsArray = tools;

    requestBody["temperature"] = 0.7;
    requestBody["max_tokens"] = 1024;
    requestBody["stream"] = false;

    QJsonDocument doc(requestBody);
    QByteArray data = doc.toJson(QJsonDocument::Compact);

    m_networkManager->post(request, data);
}

void GeminiAIClient::on_networkReplyFinished(QNetworkReply *reply) {
    reply->deleteLater();

    if (!reply) {
        emit requestFinished("Gemini: 不明なエラーが発生しました (Reply is null)", false, 0);
        return;
    }

    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray responseData = reply->readAll();

    if (reply->error() != QNetworkReply::NoError) {
        QString errorMsg = QString("Gemini APIエラー: HTTP %1 %2").arg(statusCode).arg(reply->errorString());
        if (!responseData.isEmpty()) {
            QJsonDocument errDoc = QJsonDocument::fromJson(responseData);
            if (!errDoc.isNull() && errDoc.isObject()) {
                QJsonObject errObj = errDoc.object();
                if (errObj.contains("error")) {
                    QJsonObject errDetail = errObj["error"].toObject();
                    if (errDetail.contains("message")) {
                        errorMsg += QString(" - %1").arg(errDetail["message"].toString());
                    }
                }
            }
        }
        emit requestFinished(errorMsg, false, statusCode);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    if (doc.isNull() || !doc.isObject()) {
        emit requestFinished("Gemini: レスポンスのパースに失敗しました", false, statusCode);
        return;
    }

    QJsonObject root = doc.object();
    QJsonArray choices = root["choices"].toArray();
    if (choices.isEmpty()) {
        emit requestFinished("Gemini: choicesが空でした", false, statusCode);
        return;
    }

    QJsonObject choice = choices[0].toObject();
    QJsonObject message = choice["message"].toObject();

    // Function Calling (tool_calls) の確認
    if (message.contains("tool_calls")) {
        QJsonArray toolCalls = message["tool_calls"].toArray();
        if (!toolCalls.isEmpty()) {
            QJsonObject toolCall = toolCalls[0].toObject();
            QString funcName = toolCall["function"].toObject()["name"].toString();
            QString argumentsStr = toolCall["function"].toObject()["arguments"].toString();
            m_activeToolCallId = toolCall["id"].toString();

            QJsonDocument argsDoc = QJsonDocument::fromJson(argumentsStr.toUtf8());
            QJsonObject argsObj = argsDoc.object();

            if (funcName == "update_nickname") {
                QString targetUser = argsObj["target_user"].toString().trimmed();
                QString nickname = argsObj["nickname"].toString().trimmed();
                QString resultText = "Error: Internal manager not found.";
                if (parent()) {
                    QMetaObject::invokeMethod(parent(), "handleNicknameUpdateRequest",
                                              Qt::DirectConnection,
                                              Q_RETURN_ARG(QString, resultText),
                                              Q_ARG(QString, targetUser),
                                              Q_ARG(QString, nickname));
                }
                m_pendingMessages.append(message);

                QJsonObject toolReplyMsg;
                toolReplyMsg["role"] = "tool";
                toolReplyMsg["tool_call_id"] = m_activeToolCallId;
                toolReplyMsg["content"] = resultText;
                m_pendingMessages.append(toolReplyMsg);

                // 再リクエスト
                QUrl url("https://generativelanguage.googleapis.com/v1beta/openai/chat/completions");
                QNetworkRequest request(url);
                request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
                request.setRawHeader("Authorization", QString("Bearer %1").arg(m_apiKey).toUtf8());

                QJsonObject requestBody;
                requestBody["model"] = m_model;
                requestBody["messages"] = m_pendingMessages;
                requestBody["tools"] = m_toolsArray;
                requestBody["temperature"] = 0.7;
                requestBody["max_tokens"] = 1024;
                requestBody["stream"] = false;

                m_networkManager->post(request, QJsonDocument(requestBody).toJson(QJsonDocument::Compact));
                return;
            } else if (funcName == "web_search") {
                QString query = argsObj["query"].toString();
                if (!query.isEmpty() && m_searchManager) {
                    m_isToolCalling = true;
                    m_pendingMessages.append(message);
                    m_searchManager->executeSearch(query);
                    return;
                }
            }
        }
    }

    QString content = message["content"].toString().trimmed();
    emit requestFinished(content, true, statusCode);
}

void GeminiAIClient::on_searchFinished(const QString &resultText, bool success) {
    if (!m_isToolCalling) return;
    m_isToolCalling = false;

    QJsonObject toolReplyMsg;
    toolReplyMsg["role"] = "tool";
    toolReplyMsg["tool_call_id"] = m_activeToolCallId;
    toolReplyMsg["content"] = success ? resultText : "検索結果は見つかりませんでした。";
    m_pendingMessages.append(toolReplyMsg);

    QUrl url("https://generativelanguage.googleapis.com/v1beta/openai/chat/completions");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(m_apiKey).toUtf8());

    QJsonObject requestBody;
    requestBody["model"] = m_model;
    requestBody["messages"] = m_pendingMessages;
    requestBody["tools"] = m_toolsArray;
    requestBody["temperature"] = 0.7;
    requestBody["max_tokens"] = 1024;
    requestBody["stream"] = false;

    m_networkManager->post(request, QJsonDocument(requestBody).toJson(QJsonDocument::Compact));
}
