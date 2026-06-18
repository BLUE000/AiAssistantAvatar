#pragma once
#include <QObject>
#include "istt_engine.h"
#include "../app_event.h"

class STTManager : public QObject {
    Q_OBJECT
private:
    ISTTEngine *m_currentEngine = nullptr;
    QString m_engineType; // "whisper" or "sapi"

public:
    explicit STTManager(QObject *parent = nullptr);
    ~STTManager();
    void setEngine(const QString &type);

signals:
    void notifyEvent(const AppEvent &event);

public slots:
    void on_startListening();
    void on_stopListening();
    void on_transcriptionFinished(const QString &text, bool success);
};
