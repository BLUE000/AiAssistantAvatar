#include "dummy_ai_client.h"
#include "ai_client_manager.h"
#include <QDebug>
#include <QRegularExpression>

DummyAIClient::DummyAIClient(QObject *parent) 
    : IAIClient(parent) 
{
    m_dummyTimer = new QTimer(this);
    m_dummyTimer->setSingleShot(true);
    
    connect(m_dummyTimer, &QTimer::timeout, this, [this]() {
        QString avatarName = "AIアシスタント";
        AIClientManager *manager = qobject_cast<AIClientManager*>(this->parent());
        if (manager) {
            avatarName = manager->avatarName();
        }
        QString mockResponse = QString("私はテスト用キャラクターの「%1」です。コメントありがとうございます！元気に稼働していますよ！")
                                   .arg(avatarName);
        emit requestFinished(mockResponse, true);
    });
}

DummyAIClient::~DummyAIClient() {
}

void DummyAIClient::setApiKey(const QString &apiKey) {
    Q_UNUSED(apiKey);
}

void DummyAIClient::sendRequest(const QString &prompt, const QList<QPair<QString, QString>> &history, const QString &sessionContext, const QString &systemInstruction) {
    Q_UNUSED(history);
    Q_UNUSED(sessionContext);
    Q_UNUSED(systemInstruction);
    
    // [システム指示: ...] や [RAG: ...] 等のブラケット指示部分をオウム返しから除去する
    QString cleanPrompt = prompt;
    cleanPrompt.remove(QRegularExpression("\\[[^\\]]*\\]\\s*"));
    m_lastPrompt = cleanPrompt.trimmed();
    
    qDebug() << "DummyAIClient: Simulating AI request processing for prompt:" << prompt << "Cleaned:" << m_lastPrompt;
    m_dummyTimer->start(2000); // 2秒後に応答
}

ProviderStatus DummyAIClient::defaultStatus() const {
    ProviderStatus s;
    s.provider      = QStringLiteral("dummy");
    s.available     = true;
    s.rpmMax        = 999999;
    s.rpmRemaining  = 999999;
    s.rpdMax        = 999999;
    s.rpdRemaining  = 999999;
    s.tpmMax        = 999999;
    s.tpmRemaining  = 999999;
    s.contextWindow = 131072;
    s.toolCall      = true;
    s.supportsDiff  = false;
    s.cost          = 0.0;
    s.latencyMs     = 0;
    return s;
}
