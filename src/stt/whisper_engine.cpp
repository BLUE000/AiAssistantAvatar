#include "whisper_engine.h"
#include <QDebug>

WhisperEngine::WhisperEngine(QObject *parent) 
    : ISTTEngine(parent) 
{
    m_dummyTimer = new QTimer(this);
    m_dummyTimer->setSingleShot(true);
    
    // 3秒後に音声認識完了をシミュレート
    connect(m_dummyTimer, &QTimer::timeout, this, [this]() {
        emit transcriptionFinished("これはローカルのWhisperによる音声認識テストです。", true);
    });
}

WhisperEngine::~WhisperEngine() {
}

bool WhisperEngine::initialize() {
    qDebug() << "WhisperEngine: Loading models from files (Simulated)...";
    return true;
}

void WhisperEngine::startListening() {
    qDebug() << "WhisperEngine: Recording audio from microphone (Simulated)...";
    m_dummyTimer->start(3000); // 3秒後に応答
}

void WhisperEngine::stopListening() {
    qDebug() << "WhisperEngine: Stopped recording.";
    m_dummyTimer->stop();
}
