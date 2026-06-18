#pragma once
#include "istt_engine.h"
#include <QTimer>

class SAPIEngine : public ISTTEngine {
    Q_OBJECT
private:
    QTimer *m_dummyTimer;

public:
    explicit SAPIEngine(QObject *parent = nullptr);
    ~SAPIEngine() override;

    bool initialize() override;
    void startListening() override;
    void stopListening() override;
};
