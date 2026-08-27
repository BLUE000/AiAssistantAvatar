#include "search_manager.h"
#include "../utils/process_utils.h"
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QDebug>

SearchManager::SearchManager(QObject *parent)
    : QObject(parent)
{
}

SearchManager::~SearchManager() {
    if (m_process) {
        if (m_process->state() != QProcess::NotRunning) {
            m_process->kill();
            m_process->waitForFinished(500);
        }
        m_process->deleteLater();
        m_process = nullptr;
    }
}

void SearchManager::setTavilyApiKey(const QString &apiKey) {
    m_tavilyApiKey = apiKey;
}

void SearchManager::setTimeoutMs(int timeoutMs) {
    if (timeoutMs > 0) {
        m_timeoutMs = timeoutMs;
    }
}

QString SearchManager::resolveExecutablePath() const {
    return ProcessUtils::resolveExecutablePath("WebSearcher");
}

void SearchManager::executeSearch(const QString &query) {
    if (m_process) {
        if (m_process->state() != QProcess::NotRunning) {
            m_process->kill();
            m_process->waitForFinished(200);
        }
        m_process->deleteLater();
        m_process = nullptr;
    }

    m_process = new QProcess(this);
    ProcessUtils::configureProcessEnvironment(*m_process);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &SearchManager::on_processFinished);

    QString exePath = resolveExecutablePath();
    QStringList args;
    args << "--query" << query;
    if (!m_tavilyApiKey.isEmpty()) {
        args << "--tavily-key" << m_tavilyApiKey;
    }
    args << "--timeout" << QString::number(m_timeoutMs);

    qDebug() << "SearchManager: Starting WebSearcher process:" << exePath << args;
    m_process->start(exePath, args);
}

void SearchManager::on_processFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (!m_process) return;

    QByteArray outputBytes = m_process->readAllStandardOutput();
    QString resultText = QString::fromUtf8(outputBytes).trimmed();

    bool success = (exitStatus == QProcess::NormalExit && exitCode == 0 && !resultText.isEmpty());
    if (resultText == "Web検索不可: 検索結果を取得できませんでした。") {
        success = false;
    }

    qDebug() << "SearchManager: Process finished. exitCode:" << exitCode << "success:" << success << "result length:" << resultText.length();
    emit searchFinished(resultText, success);
}

QString SearchManager::executeSearchSync(const QString &query) {
    QProcess process;
    ProcessUtils::configureProcessEnvironment(process);
    QString exePath = resolveExecutablePath();
    QStringList args;
    args << "--query" << query;
    if (!m_tavilyApiKey.isEmpty()) {
        args << "--tavily-key" << m_tavilyApiKey;
    }
    args << "--timeout" << QString::number(m_timeoutMs);

    qDebug() << "SearchManager: Starting WebSearcher sync process:" << exePath << args;
    process.start(exePath, args);
    
    // プロセス全体の安全待機時間（各プロバイダタイムアウト×2 + マージン）
    int maxWaitMs = m_timeoutMs * 2 + 2000;
    if (!process.waitForFinished(maxWaitMs)) {
        qWarning() << "SearchManager: WebSearcher process timed out, killing.";
        process.kill();
        process.waitForFinished(500);
        return QString();
    }

    if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
        QString result = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        if (result == "Web検索不可: 検索結果を取得できませんでした。") {
            return QString();
        }
        return result;
    }

    qWarning() << "SearchManager: WebSearcher failed with exit code:" << process.exitCode();
    return QString();
}
