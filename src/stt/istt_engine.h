#pragma once
#include <QObject>

class ISTTEngine : public QObject {
    Q_OBJECT
public:
    explicit ISTTEngine(QObject *parent = nullptr);
    virtual ~ISTTEngine();
    virtual bool initialize() = 0;
    virtual void startListening() = 0;
    virtual void stopListening() = 0;
    
signals:
    // 文字起こし完了時の内部通知
    void transcriptionFinished(const QString &text, bool success);
};
