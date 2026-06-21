#pragma once
#include "isearch_provider.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>

class DuckDuckGoSearchProvider : public ISearchProvider {
    Q_OBJECT
private:
    QNetworkAccessManager *m_networkManager;

public:
    explicit DuckDuckGoSearchProvider(QObject *parent = nullptr);
    void search(const QString &query) override;

private slots:
    void on_replyFinished(QNetworkReply *reply);
};
