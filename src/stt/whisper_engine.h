#pragma once
#include "istt_engine.h"
#include <QTimer>

class WhisperEngine : public ISTTEngine {
    Q_OBJECT
private:
    QTimer *m_dummyTimer;

public:
    explicit WhisperEngine(QObject *parent = nullptr);
    ~WhisperEngine() override;

    bool initialize() override;
    void startListening() override;
    void stopListening() override;
};
