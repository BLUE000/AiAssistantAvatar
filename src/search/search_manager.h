#pragma once
#include <QObject>
#include <QString>

class ISearchProvider;

class SearchManager : public QObject {
    Q_OBJECT
private:
    ISearchProvider *m_currentProvider = nullptr;
    QString m_tavilyApiKey;
    QString m_query;
    bool m_useTavily = false;

    void startNextProvider();

public:
    explicit SearchManager(QObject *parent = nullptr);
    ~SearchManager();
    void setTavilyApiKey(const QString &apiKey);
    void executeSearch(const QString &query);
    QString executeSearchSync(const QString &query);

signals:
    void searchFinished(const QString &resultText, bool success);

private slots:
    void on_providerFinished(const QString &resultText, bool success);
};
