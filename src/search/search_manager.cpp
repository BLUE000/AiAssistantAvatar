#include "search_manager.h"
#include "tavily_search_provider.h"
#include "duckduckgo_search_provider.h"
#include <QDebug>

SearchManager::SearchManager(QObject *parent)
    : QObject(parent)
{
}

SearchManager::~SearchManager() {
    if (m_currentProvider) {
        m_currentProvider->deleteLater();
    }
}

void SearchManager::setTavilyApiKey(const QString &apiKey) {
    m_tavilyApiKey = apiKey;
}

void SearchManager::executeSearch(const QString &query) {
    m_query = query;
    m_useTavily = !m_tavilyApiKey.isEmpty();
    startNextProvider();
}

void SearchManager::startNextProvider() {
    if (m_currentProvider) {
        m_currentProvider->deleteLater();
        m_currentProvider = nullptr;
    }

    if (m_useTavily) {
        qDebug() << "SearchManager: Attempting search with Tavily...";
        TavilySearchProvider *tavily = new TavilySearchProvider(m_tavilyApiKey, this);
        connect(tavily, &ISearchProvider::searchFinished,
                this, &SearchManager::on_providerFinished);
        m_currentProvider = tavily;
        tavily->search(m_query);
    } else {
        qDebug() << "SearchManager: Attempting search with DuckDuckGo...";
        DuckDuckGoSearchProvider *ddg = new DuckDuckGoSearchProvider(this);
        connect(ddg, &ISearchProvider::searchFinished,
                this, &SearchManager::on_providerFinished);
        m_currentProvider = ddg;
        ddg->search(m_query);
    }
}

void SearchManager::on_providerFinished(const QString &resultText, bool success) {
    if (success) {
        qDebug() << "SearchManager: Search succeeded.";
        emit searchFinished(resultText, true);
    } else if (m_useTavily) {
        qWarning() << "SearchManager: Tavily search failed, falling back to DuckDuckGo. Error:" << resultText;
        m_useTavily = false;
        startNextProvider();
    } else {
        qWarning() << "SearchManager: All search providers failed.";
        emit searchFinished(resultText, false);
    }
}
