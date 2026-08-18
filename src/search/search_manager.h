#pragma once
#include <QObject>
#include <QString>
#include <QProcess>

class SearchManager : public QObject {
    Q_OBJECT
private:
    QString m_tavilyApiKey;
    int m_timeoutMs = 5000;
    QProcess *m_process = nullptr;

    QString resolveExecutablePath() const;

public:
    explicit SearchManager(QObject *parent = nullptr);
    ~SearchManager();

    void setTavilyApiKey(const QString &apiKey);
    void setTimeoutMs(int timeoutMs);
    void executeSearch(const QString &query);
    QString executeSearchSync(const QString &query);

signals:
    void searchFinished(const QString &resultText, bool success);

private slots:
    void on_processFinished(int exitCode, QProcess::ExitStatus exitStatus);
};
