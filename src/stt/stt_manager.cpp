#include "stt_manager.h"
#include "whisper_engine.h"
#include "sapi_engine.h"
#include <QDebug>

STTManager::STTManager(QObject *parent) 
    : QObject(parent), m_engineType("sapi") 
{
    // 初期エンジンとして Windows SAPI を設定
    setEngine("sapi");
}

STTManager::~STTManager() {
    delete m_currentEngine;
}

void STTManager::setEngine(const QString &type) {
    if (m_currentEngine && m_engineType == type) return;
    
    qDebug() << "STTManager: Changing engine to" << type;
    
    // 古いエンジンを破棄
    if (m_currentEngine) {
        m_currentEngine->stopListening();
        m_currentEngine->disconnect(this);
        delete m_currentEngine;
        m_currentEngine = nullptr;
    }

    m_engineType = type;
    if (type == "whisper") {
        m_currentEngine = new WhisperEngine(this);
    } else {
        m_currentEngine = new SAPIEngine(this);
    }

    // 文字起こし完了のシグナルを接続
    connect(m_currentEngine, &ISTTEngine::transcriptionFinished,
            this, &STTManager::on_transcriptionFinished);
            
    m_currentEngine->initialize();
}

void STTManager::on_startListening() {
    qDebug() << "STTManager: Starting listening...";
    
    AppEvent event;
    event.type = EventType::VoiceInputStarted;
    event.source = "STTManager";
    emit notifyEvent(event);

    if (m_currentEngine) {
        m_currentEngine->startListening();
    }
}

void STTManager::on_stopListening() {
    qDebug() << "STTManager: Stopping listening...";
    if (m_currentEngine) {
        m_currentEngine->stopListening();
    }
}

void STTManager::on_transcriptionFinished(const QString &text, bool success) {
    qDebug() << "STTManager: Transcription finished. Success:" << success << "Text:" << text;
    
    AppEvent event;
    event.source = "STTManager";
    if (success) {
        event.type = EventType::VoiceInputCompleted;
        event.text = text;
    } else {
        event.type = EventType::ErrorOccurred;
        event.text = "音声認識に失敗しました。";
    }
    emit notifyEvent(event);
}
