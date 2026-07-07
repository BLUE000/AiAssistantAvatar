#include "dummy_ai_client.h"
#include <QDebug>
#include <QRegularExpression>

DummyAIClient::DummyAIClient(QObject *parent) 
    : IAIClient(parent) 
{
    m_dummyTimer = new QTimer(this);
    m_dummyTimer->setSingleShot(true);
    
    connect(m_dummyTimer, &QTimer::timeout, this, [this]() {
        QString mockResponse = QString("「%1」についてですね！私はテスト用のAIアシスタントです。元気に稼働していますよ！").arg(m_lastPrompt);
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
