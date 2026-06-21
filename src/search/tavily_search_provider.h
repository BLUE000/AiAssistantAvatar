#pragma once
#include "isearch_provider.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>

class TavilySearchProvider : public ISearchProvider {
    Q_OBJECT
private:
    QNetworkAccessManager *m_networkManager;
    QString m_apiKey;

public:
    explicit TavilySearchProvider(const QString &apiKey, QObject *parent = nullptr);
    void search(const QString &query) override;

private slots:
    void on_replyFinished(QNetworkReply *reply);
};
