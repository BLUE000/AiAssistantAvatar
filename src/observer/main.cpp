#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include "community_observer_engine.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("CommunityObserver");
    QCoreApplication::setApplicationVersion("1.0.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("AiAssistantAvatar CommunityObserver CLI (Log Collection & Anomaly Detection)");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption evalOption(QStringList() << "eval" << "e", "Evaluate user message for tone changes.");
    parser.addOption(evalOption);

    QCommandLineOption recordOption(QStringList() << "record" << "r", "Record user message to local logs.");
    parser.addOption(recordOption);

    QCommandLineOption userOption(QStringList() << "user" << "u", "Target user ID or display name.", "username");
    parser.addOption(userOption);

    QCommandLineOption platformOption(QStringList() << "platform" << "p", "Platform name (twitch/discord/direct).", "platform", "twitch");
    parser.addOption(platformOption);

    QCommandLineOption textOption(QStringList() << "text" << "t", "User message text.", "message");
    parser.addOption(textOption);

    QCommandLineOption inspectOption("inspect", "Inspect user history and summary.");
    parser.addOption(inspectOption);

    QCommandLineOption vacuumOption("vacuum", "Clean up old logs (specify max days).", "days", "60");
    parser.addOption(vacuumOption);

    QCommandLineOption logsDirOption("logs-dir", "Base directory for observer logs.", "path", "Config/observer_logs");
    parser.addOption(logsDirOption);

    parser.process(app);

    CommunityObserverEngine engine(parser.value(logsDirOption));

    QString user = parser.value(userOption);
    QString platform = parser.value(platformOption);
    QString text = parser.value(textOption);

    // 1. inspect モード
    if (parser.isSet(inspectOption)) {
        QJsonObject info = engine.inspectUser(platform, user);
        QJsonDocument doc(info);
        std::cout << doc.toJson(QJsonDocument::Indented).toStdString() << std::endl;
        return 0;
    }

    // 2. vacuum モード
    if (parser.isSet(vacuumOption)) {
        int days = parser.value(vacuumOption).toInt();
        if (days <= 0) days = 60;
        int cleaned = engine.vacuumLogs(days);
        QJsonObject res;
        res["status"] = "success";
        res["cleaned_files"] = cleaned;
        QJsonDocument doc(res);
        std::cout << doc.toJson(QJsonDocument::Compact).toStdString() << std::endl;
        return 0;
    }

    bool doEval = parser.isSet(evalOption);
    bool doRecord = parser.isSet(recordOption);

    // デフォルト（オプション未指定時）は record と eval の両方を実行
    if (!doEval && !doRecord) {
        doEval = true;
        doRecord = true;
    }

    ObserverEvaluationResult evalResult;
    if (doEval && doRecord) {
        evalResult = engine.recordAndEvaluate(platform, user, text);
    } else if (doEval) {
        evalResult = engine.evaluateMessage(platform, user, text);
    } else if (doRecord) {
        engine.recordMessage(platform, user, text);
        evalResult.statusString = "Recorded";
        evalResult.user = user;
        evalResult.platform = platform;
    }

    QJsonDocument doc(evalResult.toJson());
    std::cout << doc.toJson(QJsonDocument::Compact).toStdString() << std::endl;
    return 0;
}
