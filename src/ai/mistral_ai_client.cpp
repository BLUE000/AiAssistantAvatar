#include "mistral_ai_client.h"
#include "ai_client_manager.h"
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
    requestBody["model"] = "mistral-small-latest"; // Function Callingに対応した標準モデル

    QJsonArray messages;
    QJsonObject systemMessage;
    systemMessage["role"] = "system";

    QString systemPrompt = "あなたはデスクトップマスコットのAIアシスタントです。フレンドリーで短い日本語で回答してください。ユーザーの入力を回答で反復しないでください。ユーザーの質問に対して独立した回答を生成してください。"
                           "ユーザーが「〇〇です」「〇〇だよ」と名乗る自己紹介や、「〇〇と呼んで」などの呼び名指定をした場合は、必ず『update_nickname』ツールを呼び出して、そのユーザーのニックネームに「〇〇」を設定してください。"
                           "また、あなたには翻訳機能があります。ユーザーが翻訳をしたい場合、チャット欄で「[ウェイクワード] trans [言語] [翻訳したいテキスト]」と入力すれば翻訳を実行できます（例：「!ai trans en こんにちは」）。[言語]を省略した場合はデフォルトで日本語に翻訳されます。ユーザーから翻訳の使い方を聞かれた場合は、この「[ウェイクワード] trans [言語] [テキスト]」というコマンドの使い方を親切に教えてあげてください。";
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

    m_toolsArray = QJsonArray();
    m_toolsArray.append(tool);

    // ツール 2: update_nickname の追加
    QJsonObject nickTool;
    nickTool["type"] = "function";
    QJsonObject nickFuncObj;
    nickFuncObj["name"] = "update_nickname";
    nickFuncObj["description"] = "Register or update a nickname or preferred name for a Twitch user. Call this when the user specifies how they want to be called, including self-introductions like 'Call me X', 'I am X', or 'Xです'.";
    
    QJsonObject nickParams;
    nickParams["type"] = "object";
    QJsonObject nickProps;
    
    QJsonObject targetProp;
    targetProp["type"] = "string";
    targetProp["description"] = "The Twitch username (ID) of the user whose nickname is to be updated. If the user refers to themselves, use their own Twitch ID.";
    nickProps["target_user"] = targetProp;
    
    QJsonObject nicknameProp;
    nicknameProp["type"] = "string";
    nicknameProp["description"] = "The new nickname or preferred name (e.g. 'Alice-chan').";
    nickProps["nickname"] = nicknameProp;
    
    nickParams["properties"] = nickProps;
    
    QJsonArray nickRequired;
    nickRequired.append("target_user");
    nickRequired.append("nickname");
    nickParams["required"] = nickRequired;
    
    nickFuncObj["parameters"] = nickParams;
    nickTool["function"] = nickFuncObj;

    m_toolsArray.append(nickTool);

    requestBody["tools"] = m_toolsArray;
    requestBody["tool_choice"] = "auto";

    QJsonDocument doc(requestBody);
    QByteArray postData = doc.toJson();

    qDebug() << "MistralAIClient: Sending request with tools, history size:" << history.size();
    qDebug() << "MistralAIClient: Request Body:" << QString::fromUtf8(postData);
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
    qDebug() << "MistralAIClient: Received response:" << QString::fromUtf8(responseData);
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
                        } else if (funcName == "update_nickname") {
                            QString toolCallId = toolCall["id"].toString();
                            QString argsStr = toolCall["function"].toObject()["arguments"].toString();
                            
                            // 引数のパース
                            QJsonDocument argsDoc = QJsonDocument::fromJson(argsStr.toUtf8());
                            QString targetUser = argsDoc.object()["target_user"].toString().trimmed();
                            QString nickname = argsDoc.object()["nickname"].toString().trimmed();

                            qDebug() << "MistralAIClient: update_nickname call detected. id:" << toolCallId << "target_user:" << targetUser << "nickname:" << nickname;

                            // tool_calls を含む assistant メッセージを履歴に追加する (必須)
                            m_pendingMessages.append(messageObj);

                            // 親の AIClientManager から呼びかけ処理を実行して、AIへ返す結果を取得
                            QString resultText = "Error: Internal manager not found.";
                            AIClientManager *manager = qobject_cast<AIClientManager*>(parent());
                            if (manager) {
                                resultText = manager->handleNicknameUpdateRequest(targetUser, nickname);
                            }

                            // ツール応答 (toolロール) をメッセージ履歴に追加
                            QJsonObject toolResponse;
                            toolResponse["role"] = "tool";
                            toolResponse["name"] = "update_nickname";
                            toolResponse["tool_call_id"] = toolCallId;
                            
                            QJsonObject contentObj;
                            contentObj["result"] = resultText;
                            toolResponse["content"] = QString::fromUtf8(QJsonDocument(contentObj).toJson(QJsonDocument::Compact));
                            
                            m_pendingMessages.append(toolResponse);

                            // 再度 Mistral に最終回答リクエストを送信
                            QUrl url("https://api.mistral.ai/v1/chat/completions");
                            QNetworkRequest request(url);
                            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
                            request.setRawHeader("Authorization", QString("Bearer %1").arg(m_apiKey).toUtf8());

                            QJsonObject requestBody;
                            requestBody["model"] = "mistral-small-latest";
                            requestBody["messages"] = m_pendingMessages;
                            requestBody["tools"] = m_toolsArray;
                            requestBody["tool_choice"] = "auto";

                            QJsonDocument doc(requestBody);
                            QByteArray postData = doc.toJson();

                            qDebug() << "MistralAIClient: Sending request back to Mistral after update_nickname execution...";
                            m_networkManager->post(request, postData);
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
    qDebug() << "MistralAIClient: Search result content:" << resultText;

    // 検索結果 (toolロール) をメッセージ履歴に追加
    QJsonObject toolResponse;
    toolResponse["role"] = "tool";
    toolResponse["name"] = "web_search";
    toolResponse["tool_call_id"] = m_activeToolCallId;
    
    // 指摘通り、ツールの応答コンテンツを JSON オブジェクトの文字列形式にする
    QJsonObject contentObj;
    contentObj["result"] = resultText;
    toolResponse["content"] = QString::fromUtf8(QJsonDocument(contentObj).toJson(QJsonDocument::Compact));
    
    m_pendingMessages.append(toolResponse);

    // 再度 Mistral に最終回答リクエストを送信
    QUrl url("https://api.mistral.ai/v1/chat/completions");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(m_apiKey).toUtf8());

    QJsonObject requestBody;
    requestBody["model"] = "mistral-small-latest";
    requestBody["messages"] = m_pendingMessages; // 検索結果を含んだメッセージ履歴
    requestBody["tools"] = m_toolsArray; // 再送信リクエストでもツール定義を渡す
    requestBody["tool_choice"] = "auto"; // モデルにツール使用権限を与える

    QJsonDocument doc(requestBody);
    QByteArray postData = doc.toJson();

    qDebug() << "MistralAIClient: Sending final response request to Mistral...";
    qDebug() << "MistralAIClient: Final Request Body:" << QString::fromUtf8(postData);
    m_networkManager->post(request, postData);
}
