#pragma once
#include <QObject>
#include <QString>

class ISearchProvider : public QObject {
    Q_OBJECT
public:
    explicit ISearchProvider(QObject *parent = nullptr);
    virtual ~ISearchProvider();
    virtual void search(const QString &query) = 0;

signals:
    void searchFinished(const QString &resultText, bool success);
};
