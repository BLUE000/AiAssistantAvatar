#include "sakura_ai_client.h"
#include "ai_client_manager.h"
#include "../search/search_manager.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QThread>
#include <QDebug>

SakuraAIClient::SakuraAIClient(QObject *parent)
    : IAIClient(parent), m_model("llm-jp-3.1-8x13b-instruct4")
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
    if (m_networkManager && m_networkManager->thread() != QThread::currentThread()) {
        m_networkManager->moveToThread(QThread::currentThread());
    }
    m_apiKey = apiKey;
}

void SakuraAIClient::setModel(const QString &model) {
    if (m_networkManager && m_networkManager->thread() != QThread::currentThread()) {
        m_networkManager->moveToThread(QThread::currentThread());
    }
    if (!model.isEmpty()) {
        m_model = model;
    }
}

void SakuraAIClient::setTavilyApiKey(const QString &tavilyKey) {
    if (m_networkManager && m_networkManager->thread() != QThread::currentThread()) {
        m_networkManager->moveToThread(QThread::currentThread());
    }
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
    if (m_networkManager && m_networkManager->thread() != QThread::currentThread()) {
        m_networkManager->moveToThread(QThread::currentThread());
    }

    if (m_apiKey.isEmpty()) {
        emit requestFinished("さくらAI APIキーが設定されていません。local_settings.json を確認してください。", false, 0);
        return;
    }

    // 検索が必要なクエリかどうかの判定 (方法B: 事前判定型 RAG)
    QString lowerPrompt = prompt.toLower();
    bool needsSearch = lowerPrompt.contains("天気") || lowerPrompt.contains("てんき") ||
                        lowerPrompt.contains("ニュース") || lowerPrompt.contains("最新") ||
                        lowerPrompt.contains("今") || lowerPrompt.contains("明日") ||
                        lowerPrompt.contains("為替") || lowerPrompt.contains("株価") ||
                        lowerPrompt.contains("weather") || lowerPrompt.contains("news");

    if (needsSearch && m_searchManager) {
        m_isPreSearching = true;
        m_pendingPrompt = prompt;
        m_pendingHistory = history;
        m_pendingSessionContext = sessionContext;
        m_pendingSystemInstruction = systemInstruction;

        qDebug() << "[SakuraAIClient] Triggering pre-search RAG for query:" << prompt;
        m_searchManager->executeSearch(prompt);
        return;
    }

    sendRealSakuraRequest(prompt, history, sessionContext, systemInstruction, "");
}

void SakuraAIClient::sendRealSakuraRequest(const QString &prompt, const QList<QPair<QString, QString>> &history, const QString &sessionContext, const QString &systemInstruction, const QString &webSearchResultContext) {
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

    QString systemPrompt = QString("あなたはデスクトップマスコットのキャラクター「%1」です。自己紹介や名前を聞かれた際は、必ず「%1」と名乗ってください。自分自身を「AIアシスタント」や「AI」といった一般名詞で呼ばず、必ずキャラクター名「%1」または「私」と名乗ってください。それ以外の名前を使用しないでください。フレンドリーで短い日本語で回答してください。ユーザーの入力を回答で反復しないでください。ユーザーの質問に対して独立した回答を生成してください。対話相手のお名前や呼び名が明示的に指定されていない場合は、相手のお名前を推測・捏造せず、お名前を呼ばずにそのまま回答してください。")
                            .arg(avatarName);

    QString adjustedInstruction = systemInstruction;
    QString searchToolInstruction = "また、ユーザーから天気、為替レート、最新ニュース、リアルタイム情報、またはあなたが最新の正確な知識を持っていない事柄について質問された場合は、自身の過去の知識で回答しようとせず、必ず『web_search』ツールを呼び出して最新情報を検索してください。";
    if (adjustedInstruction.contains(searchToolInstruction)) {
        adjustedInstruction.replace(searchToolInstruction,
            "また、ユーザーから天気、為替レート、最新ニュース、リアルタイム情報について質問された際、プロンプト内に【最新Web検索結果（情報源）】が与えられている場合は、その情報を参照して親切に回答してください。");
    }

    if (!adjustedInstruction.isEmpty()) {
        systemPrompt += "\n" + adjustedInstruction;
    }
    if (!webSearchResultContext.isEmpty()) {
        systemPrompt += "\n\n" + webSearchResultContext;
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

    requestBody["messages"] = messages;
    requestBody["max_tokens"] = 1024;

    // ※ さくらAI (vLLM) の Bad Request (400) 回避のため tools は送信しない

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

    QString replyText = messageObj["content"].toString();
    emit requestFinished(replyText.trimmed(), true, 200);
}

void SakuraAIClient::on_searchFinished(const QString &resultText, bool success) {
    if (!m_isPreSearching) return;
    m_isPreSearching = false;

    QString searchContext;
    if (success && !resultText.isEmpty()) {
        searchContext = QString("【最新Web検索結果（情報源）】\n%1\n※上記の情報に基づいて回答してください。").arg(resultText);
    }

    sendRealSakuraRequest(m_pendingPrompt, m_pendingHistory, m_pendingSessionContext, m_pendingSystemInstruction, searchContext);
}
