#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QTimer>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <iostream>

#include "ai/groq_ai_client.h"
#include "utils/config_utils.h"
#include "utils/json_comment_remover.h"


int main(int argc, char *argv[]) {
#ifdef Q_OS_WIN
    system("chcp 65001 > nul");
#endif

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("GroqChatter");
    QCoreApplication::setApplicationVersion("1.0.0");

    // 親ディレクトリの Qt プラグイン（tls, platforms等）およびカレント探索パスを登録 (F-47)
    QCoreApplication::addLibraryPath(QCoreApplication::applicationDirPath() + "/..");
    QCoreApplication::addLibraryPath(QCoreApplication::applicationDirPath());

    QCommandLineParser parser;
    parser.setApplicationDescription("AI Assistant Avatar - Groq AI CLI Chatter");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption promptOption(QStringList() << "p" << "prompt", "Input user prompt text (required)", "prompt");
    QCommandLineOption systemOption(QStringList() << "s" << "system", "System instruction prompt", "system");
    QCommandLineOption modelOption(QStringList() << "m" << "model", "Groq model name (default: from local_settings.json or auto-selected)", "model", "");
    QCommandLineOption apiKeyOption(QStringList() << "k" << "api-key", "Groq API key (gsk_...)", "key");
    QCommandLineOption configOption(QStringList() << "c" << "config", "Path to local_settings.json", "path");
    QCommandLineOption formatOption(QStringList() << "f" << "format", "Output format ('text' or 'json', default: text)", "format", "text");
    QCommandLineOption timeoutOption("timeout", "Timeout in milliseconds (default: 8000)", "timeout", "8000");

    parser.addOption(promptOption);
    parser.addOption(systemOption);
    parser.addOption(modelOption);
    parser.addOption(apiKeyOption);
    parser.addOption(configOption);
    parser.addOption(formatOption);
    parser.addOption(timeoutOption);

    parser.process(app);

    QString prompt = parser.value(promptOption).trimmed();
    QString systemInstruction = parser.value(systemOption).trimmed();
    QString model = parser.value(modelOption).trimmed();
    QString apiKey = parser.value(apiKeyOption).trimmed();
    QString configPath = parser.value(configOption).trimmed();
    QString format = parser.value(formatOption).trimmed().toLower();
    int timeoutMs = parser.value(timeoutOption).toInt();
    if (timeoutMs <= 0) timeoutMs = 8000;

    if (prompt.isEmpty()) {
        std::cerr << "Error: --prompt is required." << std::endl;
        parser.showHelp(2);
        return 2;
    }

    if (configPath.isEmpty()) {
        configPath = ConfigUtils::resolveConfigFilePath("local_settings.json");
    }

    if (QFile::exists(configPath)) {
        QFile file(configPath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QByteArray data = JsonCommentRemover::stripHashComments(file.readAll());
            file.close();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (!doc.isNull() && doc.isObject()) {
                QJsonObject obj = doc.object();
                if (apiKey.isEmpty()) {
                    apiKey = obj.value("groq_api_key").toString().trimmed();
                }
                if (model.isEmpty() && obj.contains("groq_model")) {
                    model = obj.value("groq_model").toString().trimmed();
                }
            }
        }
    }

    if (apiKey.isEmpty()) {
        std::cerr << "Error: Groq API key is missing. Specify --api-key or configure groq_api_key in local_settings.json" << std::endl;
        return 1;
    }

    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);

    auto outputResult = [&](const QString &generatedText, bool success) {
        timeoutTimer.stop();
        if (format == "json") {
            QJsonObject resObj;
            resObj["status"] = success ? "success" : "error";
            resObj["model"] = model;
            resObj["text"] = generatedText;
            QJsonDocument outDoc(resObj);
            std::cout << outDoc.toJson(QJsonDocument::Compact).toStdString() << std::endl;
        } else {
            std::cout << generatedText.toUtf8().constData() << std::endl;
        }
        app.exit(success ? 0 : 1);
    };

    QObject::connect(&timeoutTimer, &QTimer::timeout, &app, [&]() {
        outputResult("Groq推論タイムアウト", false);
    });
    timeoutTimer.start(timeoutMs);

    auto *client = new GroqAIClient(&app);
    client->setApiKey(apiKey);
    if (!model.isEmpty()) {
        client->setModel(model);
    }

    QObject::connect(client, &IAIClient::requestFinished, &app, [=](const QString &response, bool reqSuccess, int httpCode) {
        Q_UNUSED(httpCode);
        if (reqSuccess && !response.trimmed().isEmpty()) {
            outputResult(response.trimmed(), true);
        } else {
            outputResult(response.trimmed().isEmpty() ? "Groq推論エラー" : response.trimmed(), false);
        }
    });

    client->sendRequest(prompt, {}, "", systemInstruction);

    return app.exec();
}
