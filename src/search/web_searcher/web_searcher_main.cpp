#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QTimer>
#include <QTextStream>
#include <iostream>
#include "search/tavily_search_provider.h"
#include "search/duckduckgo_search_provider.h"



int main(int argc, char *argv[]) {
    // Windows コンソールで UTF-8 出力を確実にする
#ifdef Q_OS_WIN
    system("chcp 65001 > nul");
#endif

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("WebSearcher");
    QCoreApplication::setApplicationVersion("1.0.0");

    // 親ディレクトリの Qt プラグイン（tls, platforms等）およびカレント探索パスを登録 (F-47)
    QCoreApplication::addLibraryPath(QCoreApplication::applicationDirPath() + "/..");
    QCoreApplication::addLibraryPath(QCoreApplication::applicationDirPath());

    QCommandLineParser parser;
    parser.setApplicationDescription("AI Assistant Avatar - Web Search CLI Module (Tavily + DuckDuckGo)");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption queryOption(QStringList() << "q" << "query", "Search query keyword", "query");
    QCommandLineOption tavilyKeyOption(QStringList() << "k" << "tavily-key", "Tavily API Key", "key");
    QCommandLineOption timeoutOption(QStringList() << "t" << "timeout", "Timeout in milliseconds for each provider (default: 5000)", "timeout", "5000");

    parser.addOption(queryOption);
    parser.addOption(tavilyKeyOption);
    parser.addOption(timeoutOption);

    parser.process(app);

    QString query = parser.value(queryOption).trimmed();
    QString tavilyKey = parser.value(tavilyKeyOption).trimmed();
    int timeoutMs = parser.value(timeoutOption).toInt();
    if (timeoutMs <= 0) timeoutMs = 5000;

    if (query.isEmpty()) {
        std::cerr << "Error: --query is required." << std::endl;
        parser.showHelp(2);
        return 2;
    }

    bool hasTavilyKey = !tavilyKey.isEmpty();
    int exitCode = 0;

    QTimer providerTimer;
    providerTimer.setSingleShot(true);

    TavilySearchProvider *tavilyProvider = nullptr;
    DuckDuckGoSearchProvider *ddgProvider = nullptr;

    auto runDuckDuckGo = [&]() {
        if (tavilyProvider) {
            tavilyProvider->deleteLater();
            tavilyProvider = nullptr;
        }

        ddgProvider = new DuckDuckGoSearchProvider(&app);
        
        QObject::connect(ddgProvider, &ISearchProvider::searchFinished, &app, [&](const QString &resultText, bool success){
            providerTimer.stop();
            if (success && !resultText.trimmed().isEmpty()) {
                std::cout << resultText.toUtf8().constData() << std::endl;
                app.exit(0);
            } else {
                std::cout << "Web検索不可: 検索結果を取得できませんでした。" << std::endl;
                app.exit(1);
            }
        });

        QObject::connect(&providerTimer, &QTimer::timeout, &app, [&](){
            std::cout << "Web検索不可: 検索結果を取得できませんでした。" << std::endl;
            app.exit(1);
        });

        providerTimer.start(timeoutMs);
        ddgProvider->search(query);
    };

    if (hasTavilyKey) {
        tavilyProvider = new TavilySearchProvider(tavilyKey, &app);

        QObject::connect(tavilyProvider, &ISearchProvider::searchFinished, &app, [&](const QString &resultText, bool success){
            providerTimer.stop();
            if (success && !resultText.trimmed().isEmpty()) {
                std::cout << resultText.toUtf8().constData() << std::endl;
                app.exit(0);
            } else {
                // Tavily 失敗時はサイレントに DuckDuckGo へフォールバック
                runDuckDuckGo();
            }
        });

        QObject::connect(&providerTimer, &QTimer::timeout, &app, [&](){
            // Tavily タイムアウト時は DuckDuckGo へフォールバック
            runDuckDuckGo();
        });

        providerTimer.start(timeoutMs);
        tavilyProvider->search(query);
    } else {
        runDuckDuckGo();
    }

    return app.exec();
}
