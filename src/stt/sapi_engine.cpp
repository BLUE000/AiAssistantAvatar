#include "sapi_engine.h"
#include <QDebug>

SAPIEngine::SAPIEngine(QObject *parent) 
    : ISTTEngine(parent) 
{
    m_dummyTimer = new QTimer(this);
    m_dummyTimer->setSingleShot(true);
    
    connect(m_dummyTimer, &QTimer::timeout, this, [this]() {
        emit transcriptionFinished("これはWindows SAPIによる音声認識の検証です。", true);
    });
}

SAPIEngine::~SAPIEngine() {
}

bool SAPIEngine::initialize() {
    qDebug() << "SAPIEngine: Initializing COM library and SAPI Recognizer (Simulated)...";
    return true;
}

void SAPIEngine::startListening() {
    qDebug() << "SAPIEngine: Activating SAPI dictation grammar...";
    m_dummyTimer->start(3000); // 3秒後に応答
}

void SAPIEngine::stopListening() {
    qDebug() << "SAPIEngine: Deactivating grammar.";
    m_dummyTimer->stop();
}
