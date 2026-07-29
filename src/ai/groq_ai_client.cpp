#include "groq_ai_client.h"
#include "ai_client_manager.h"
#include "../search/search_manager.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QThread>
#include <QDebug>

GroqAIClient::GroqAIClient(QObject *parent)
    : IAIClient(parent), m_isToolCalling(false), m_model("llama-3.1-8b-instant")
{
    m_networkManager = new QNetworkAccessManager(this);
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &GroqAIClient::on_networkReplyFinished);

    m_searchManager = new SearchManager(this);
    connect(m_searchManager, &SearchManager::searchFinished,
            this, &GroqAIClient::on_searchFinished);
}

GroqAIClient::~GroqAIClient() {
}

void GroqAIClient::setApiKey(const QString &apiKey) {
    m_apiKey = apiKey;
}

void GroqAIClient::setModel(const QString &model) {
    if (!model.isEmpty()) {
        m_model = model;
    }
}

void GroqAIClient::setTavilyApiKey(const QString &tavilyKey) {
    if (m_searchManager) {
        m_searchManager->setTavilyApiKey(tavilyKey);
    }
}

void GroqAIClient::sendRequest(const QString &prompt, const QList<QPair<QString, QString>> &history, const QString &sessionContext, const QString &systemInstruction) {
    if (m_apiKey.isEmpty()) {
        emit requestFinished("Groq APIキーが設定されていません。local_settings.json を確認してください。", false, 0);
        return;
    }

    m_isToolCalling = false;
    m_pendingPrompt = prompt;

    QUrl url("https://api.groq.com/openai/v1/chat/completions");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(m_apiKey).toUtf8());

    QJsonObject requestBody;
    requestBody["model"] = m_model;

    QJsonArray messages;
    QJsonObject systemMessage;
    systemMessage["role"] = "system";

    QString avatarName = "AIアシスタント";
    AIClientManager *manager = qobject_cast<AIClientManager*>(parent());
    if (manager) {
        avatarName = manager->avatarName();
    }

    QString systemPrompt = QString("あなたはデスクトップマスコットのキャラクター「%1」です。自己紹介や名前を聞かれた際は、必ず「%1」と名乗ってください。自分自身を「AIアシスタント」や「AI」といった一般名詞で呼ばず、必ずキャラクター名「%1」または「私」と名乗ってください。それ以外の名前を使用しないでください。フレンドリーで短い日本語で回答してください。ユーザーの入力を回答で反復しないでください。ユーザーの質問に対して独立した回答を生成してください。対話相手のお名前や呼び名が明示的に指定されていない場合は、相手のお名前を推測・捏造せず、お名前を呼ばずにそのまま回答してください。")
                            .arg(avatarName)
                           + "ユーザーが「〇〇です」「〇〇だよ」と名乗る自己紹介や、「〇〇と呼んで」などの呼び名指定をした場合は、必ず『update_nickname』ツールを呼び出して、そのユーザーのニックネームに「〇〇」を設定してください。"
                             "また、ユーザーから天気、為替レート、最新ニュース、リアルタイム情報、またはあなたが最新の正確な知識を持っていない事柄について質問された場合は、自身の過去の知識で回答しようとせず、必ず『web_search』ツールを呼び出して最新情報を検索してください。"
                             "また、あなたには翻訳機能があります。ユーザーが翻訳をしたい場合、チャット欄で「[ウェイクワード] trans [言語] [翻訳したいテキスト]」と入力すれば翻訳を実行できます（例：「!ai trans en こんにちは」）。[言語]を省略した場合はデフォルトで日本語に翻訳されます。ユーザーから翻訳の使い方を聞かれた場合は、この「[ウェイクワード] trans [言語] [テキスト]」というコマンドの使い方を親切に教えてあげてください。";
    
    if (!systemInstruction.isEmpty()) {
        systemPrompt += "\n\n" + systemInstruction;
    }
    
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
    m_toolsArray = QJsonArray();

    if (manager && manager->importState() == KnowledgeImportState::QandAMode) {
        QJsonObject importTool;
        importTool["type"] = "function";
        QJsonObject importFuncObj;
        importFuncObj["name"] = "finalize_knowledge_import";
        importFuncObj["description"] = "Finalize the registration of the current Markdown knowledge file. Call this when the user agrees to finalize the import and all metadata (title, description, keywords) are determined.";
        
        QJsonObject importParams;
        importParams["type"] = "object";
        QJsonObject importProps;
        
        QJsonObject titleProp;
        titleProp["type"] = "string";
        titleProp["description"] = "The title / name of the knowledge being registered (e.g. 'Plugin API Guide').";
        importProps["title"] = titleProp;
        
        QJsonObject descProp;
        descProp["type"] = "string";
        descProp["description"] = "A short summary / description of what the file content contains.";
        importProps["description"] = descProp;
        
        QJsonObject kwProp;
        kwProp["type"] = "array";
        QJsonObject kwItems;
        kwItems["type"] = "string";
        kwProp["items"] = kwItems;
        kwProp["description"] = "A list of 3 to 5 key trigger words to match user prompts to this knowledge file in future conversations (e.g. ['plugin', 'API', 'specs']).";
        importProps["keywords"] = kwProp;
        
        importParams["properties"] = importProps;
        
        QJsonArray importRequired;
        importRequired.append("title");
        importRequired.append("description");
        importRequired.append("keywords");
        importParams["required"] = importRequired;
        
        importFuncObj["parameters"] = importParams;
        importTool["function"] = importFuncObj;

        m_toolsArray.append(importTool);
    } else {
        QJsonObject tool;
        tool["type"] = "function";
        QJsonObject functionObj;
        functionObj["name"] = "web_search";
        functionObj["description"] = "天気、最新ニュース、リアルタイム情報（例：気温、降水確率、台風、地震、株価、スポーツ結果など）を取得するために使用します。ユーザーがこれらの情報を要求した場合は、必ずこのツールを呼び出して最新の情報を取得してください。";
        
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

        m_toolsArray.append(tool);

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
    }

    requestBody["tools"] = m_toolsArray;
    requestBody["tool_choice"] = "auto";

    QJsonDocument doc(requestBody);
    QByteArray postData = doc.toJson();

    qDebug() << "GroqAIClient: Sending request with tools, history size:" << history.size();
    qDebug() << "GroqAIClient: Request Body:" << QString::fromUtf8(postData);
    m_networkManager->post(request, postData);
}

void GroqAIClient::on_networkReplyFinished(QNetworkReply *reply) {
    AIClientManager *manager = qobject_cast<AIClientManager*>(parent());
    if (manager) {
        manager->tracker().updateFromReply(QStringLiteral("groq"), reply);
    }

    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString errorMsg = QString("ネットワークエラー: %1 (%2)")
                            .arg(reply->errorString())
                            .arg(httpCode);
        qWarning() << "GroqAIClient Error:" << errorMsg;
        emit requestFinished(errorMsg, false, httpCode);
        return;
    }

    QByteArray responseData = reply->readAll();
    qDebug() << "GroqAIClient: Received response:" << QString::fromUtf8(responseData);
    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    if (doc.isNull() || !doc.isObject()) {
        emit requestFinished("レスポンスJSONの解析に失敗しました。", false, 0);
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

                            qDebug() << "GroqAIClient: Function call detected. id:" << m_activeToolCallId << "query:" << query;
                            
                            // tool_calls を含む assistant メッセージを履歴に追加する (Cerebras APIの仕様上必須)
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

                            qDebug() << "GroqAIClient: update_nickname call detected. id:" << toolCallId << "target_user:" << targetUser << "nickname:" << nickname;

                            // tool_calls を含む assistant メッセージを履歴に追加する
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

                            // 再度 Cerebras に最終回答リクエストを送信
                            QUrl url("https://api.groq.com/openai/v1/chat/completions");
                            QNetworkRequest request(url);
                            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
                            request.setRawHeader("Authorization", QString("Bearer %1").arg(m_apiKey).toUtf8());

                            QJsonObject requestBody;
                            requestBody["model"] = m_model;
                            requestBody["messages"] = m_pendingMessages;
                            requestBody["tools"] = m_toolsArray;
                            requestBody["tool_choice"] = "auto";

                            QJsonDocument doc(requestBody);
                            QByteArray postData = doc.toJson();

                            qDebug() << "GroqAIClient: Sending request back to Groq after update_nickname execution...";
                            m_networkManager->post(request, postData);
                            return;
                        } else if (funcName == "finalize_knowledge_import") {
                            QString toolCallId = toolCall["id"].toString();
                            QString argsStr = toolCall["function"].toObject()["arguments"].toString();
                            
                            // 引数のパース
                            QJsonDocument argsDoc = QJsonDocument::fromJson(argsStr.toUtf8());
                            QString title = argsDoc.object()["title"].toString().trimmed();
                            QString description = argsDoc.object()["description"].toString().trimmed();
                            QJsonArray kwJsonArr = argsDoc.object()["keywords"].toArray();
                            QStringList keywords;
                            for (const QJsonValue &v : kwJsonArr) {
                                QString kw = v.toString().trimmed();
                                if (!kw.isEmpty()) keywords.append(kw);
                            }

                            qDebug() << "GroqAIClient: finalize_knowledge_import call detected. id:" << toolCallId << "title:" << title << "description:" << description << "keywords:" << keywords;

                            m_pendingMessages.append(messageObj);

                            QString resultText = "Error: Internal manager not found.";
                            AIClientManager *manager = qobject_cast<AIClientManager*>(parent());
                            if (manager) {
                                resultText = manager->finalizeKnowledgeImport(title, description, keywords);
                            }

                            QJsonObject toolResponse;
                            toolResponse["role"] = "tool";
                            toolResponse["name"] = "finalize_knowledge_import";
                            toolResponse["tool_call_id"] = toolCallId;
                            
                            QJsonObject contentObj;
                            contentObj["result"] = resultText;
                            toolResponse["content"] = QString::fromUtf8(QJsonDocument(contentObj).toJson(QJsonDocument::Compact));
                            
                            m_pendingMessages.append(toolResponse);

                            QUrl url("https://api.groq.com/openai/v1/chat/completions");
                            QNetworkRequest request(url);
                            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
                            request.setRawHeader("Authorization", QString("Bearer %1").arg(m_apiKey).toUtf8());

                            QJsonObject requestBody;
                            requestBody["model"] = m_model;
                            requestBody["messages"] = m_pendingMessages;
                            requestBody["tools"] = m_toolsArray;
                            requestBody["tool_choice"] = "auto";

                            QJsonDocument doc(requestBody);
                            QByteArray postData = doc.toJson();

                            qDebug() << "GroqAIClient: Sending request back to Groq after finalize_knowledge_import execution...";
                            m_networkManager->post(request, postData);
                            return;
                        }
                    }
                }

                // 通常のテキスト応答
                QString replyText = messageObj["content"].toString();
                emit requestFinished(replyText.trimmed(), true, 200);
                return;
            }
        }
    }

    emit requestFinished("レスポンスから適切なメッセージが見つかりませんでした。", false, 0);
}

void GroqAIClient::on_searchFinished(const QString &resultText, bool success) {
    qDebug() << "GroqAIClient: Search finished. Success:" << success << "Result length:" << resultText.length();
    qDebug() << "GroqAIClient: Search result content:" << resultText;

    // 検索結果 (toolロール) をメッセージ履歴に追加
    QJsonObject toolResponse;
    toolResponse["role"] = "tool";
    toolResponse["name"] = "web_search";
    toolResponse["tool_call_id"] = m_activeToolCallId;
    
    QJsonObject contentObj;
    contentObj["result"] = resultText;
    toolResponse["content"] = QString::fromUtf8(QJsonDocument(contentObj).toJson(QJsonDocument::Compact));
    
    m_pendingMessages.append(toolResponse);

    // 再度 Cerebras に最終回答リクエストを送信
    QUrl url("https://api.groq.com/openai/v1/chat/completions");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(m_apiKey).toUtf8());

    QJsonObject requestBody;
    requestBody["model"] = m_model;
    requestBody["messages"] = m_pendingMessages; // 検索結果を含んだメッセージ履歴
    requestBody["tools"] = m_toolsArray; // 再送信リクエストでもツール定義を渡す
    requestBody["tool_choice"] = "auto"; // モデルにツール使用権限を与える

    QJsonDocument doc(requestBody);
    QByteArray postData = doc.toJson();

    qDebug() << "GroqAIClient: Sending final response request to Groq...";
    qDebug() << "GroqAIClient: Final Request Body:" << QString::fromUtf8(postData);
    m_networkManager->post(request, postData);
}

ProviderStatus GroqAIClient::defaultStatus() const {
    ProviderStatus s;
    s.provider      = QStringLiteral("groq");
    s.available     = true;
    s.rpmMax        = 30;
    s.rpmRemaining  = 30;
    s.rpdMax        = 14400;
    s.rpdRemaining  = 14400;
    s.tpmMax        = 131072;
    s.tpmRemaining  = 131072;
    s.contextWindow = 131072;
    s.toolCall      = true;
    s.supportsDiff  = false;
    s.cost          = 0.0;
    s.latencyMs     = 0;
    return s;
}
