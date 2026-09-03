#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QTimer>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <iostream>

#include "ai/twitch_helix_client.h"
#include "ai/ai_client_manager.h"
#include "ai/mistral_ai_client.h"
#include "ai/groq_ai_client.h"
#include "ai/gemini_ai_client.h"
#include "ai/huggingface_ai_client.h"
#include "ai/openrouter_ai_client.h"
#include "ai/sakura_ai_client.h"
#include "ai/dummy_ai_client.h"
#include "utils/config_utils.h"
#include "utils/json_comment_remover.h"


int main(int argc, char *argv[]) {
#ifdef Q_OS_WIN
    system("chcp 65001 > nul");
#endif

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("TwitchIntroGenerator");
    QCoreApplication::setApplicationVersion("1.0.0");

    // 親ディレクトリの Qt プラグインおよびカレント探索パスを登録 (F-47)
    QCoreApplication::addLibraryPath(QCoreApplication::applicationDirPath() + "/..");
    QCoreApplication::addLibraryPath(QCoreApplication::applicationDirPath());

    QCommandLineParser parser;
    parser.setApplicationDescription("AI Assistant Avatar - Twitch Creator Introduction CLI Generator");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption userOption(QStringList() << "u" << "user", "Target Twitch user login", "user");
    QCommandLineOption modeOption(QStringList() << "m" << "mode", "Context mode ('raid' or 'conversation', default: conversation)", "mode", "conversation");
    QCommandLineOption lengthOption(QStringList() << "l" << "length", "Length hint ('short', 'standard', 'long', default: standard)", "length", "standard");
    QCommandLineOption toneOption(QStringList() << "t" << "tone", "Tone hint (default: '明るく元気な口調で！')", "tone", "明るく元気な口調で！");
    QCommandLineOption configOption(QStringList() << "c" << "config", "Path to local_settings.json", "path");
    QCommandLineOption formatOption(QStringList() << "f" << "format", "Output format ('text' or 'json', default: text)", "format", "text");
    QCommandLineOption timeoutOption("timeout", "Timeout in milliseconds (default: 15000)", "timeout", "15000");

    parser.addOption(userOption);
    parser.addOption(modeOption);
    parser.addOption(lengthOption);
    parser.addOption(toneOption);
    parser.addOption(configOption);
    parser.addOption(formatOption);
    parser.addOption(timeoutOption);

    parser.process(app);

    QString user = parser.value(userOption).trimmed();
    QString mode = parser.value(modeOption).trimmed().toLower();
    QString length = parser.value(lengthOption).trimmed();
    QString tone = parser.value(toneOption).trimmed();
    QString configPath = parser.value(configOption).trimmed();
    QString format = parser.value(formatOption).trimmed().toLower();
    int timeoutMs = parser.value(timeoutOption).toInt();
    if (timeoutMs <= 0) timeoutMs = 15000;

    if (user.isEmpty()) {
        std::cerr << "Error: --user is required." << std::endl;
        parser.showHelp(2);
        return 2;
    }

    if (configPath.isEmpty()) {
        configPath = ConfigUtils::resolveConfigFilePath("local_settings.json");
    }

    QString twitchToken;
    QString twitchClientId;
    QString twitchChannel;
    QString twitchUsername;
    bool shoutoutUseCommand = true;
    QString aiProvider = "dummy";
    QString mistralKey, groqKey, geminiKey, hfKey, openrouterKey, sakuraKey;
    QString mistralModel, groqModel, geminiModel, hfModel, openrouterModel, sakuraModel;

    if (QFile::exists(configPath)) {
        QFile file(configPath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QByteArray data = JsonCommentRemover::stripHashComments(file.readAll());
            file.close();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (!doc.isNull() && doc.isObject()) {
                QJsonObject obj = doc.object();
                twitchToken = obj.value("twitch_oauth_token").toString();
                twitchClientId = obj.value("twitch_client_id").toString();
                twitchChannel = obj.value("twitch_channel").toString();
                twitchUsername = obj.value("twitch_username").toString();
                if (obj.contains("shoutout_use_command")) {
                    shoutoutUseCommand = obj.value("shoutout_use_command").toBool(true);
                }
                aiProvider = obj.value("ai_provider").toString("dummy").toLower();
                mistralKey = obj.value("mistral_api_key").toString();
                groqKey = obj.value("groq_api_key").toString();
                geminiKey = obj.value("gemini_api_key").toString();
                hfKey = obj.value("huggingface_api_key").toString();
                openrouterKey = obj.value("openrouter_api_key").toString();
                sakuraKey = obj.value("sakura_api_key").toString();

                mistralModel = obj.value("mistral_model").toString();
                groqModel = obj.value("groq_model").toString();
                geminiModel = obj.value("gemini_model").toString();
                hfModel = obj.value("huggingface_model").toString();
                openrouterModel = obj.value("openrouter_model").toString();
                sakuraModel = obj.value("sakura_model").toString();
            }
        }
    }

    TwitchHelixClient helixClient;
    helixClient.setCredentials(twitchToken, twitchClientId);

    // レイド時限定: Twitch 公式 /shoutout REST API の送信（自己宛除外）
    if (mode == "raid" && shoutoutUseCommand) {
        bool isSelf = (!twitchChannel.isEmpty() && user.compare(twitchChannel, Qt::CaseInsensitive) == 0) ||
                      (!twitchUsername.isEmpty() && user.compare(twitchUsername, Qt::CaseInsensitive) == 0);
        if (!isSelf) {
            QString broadcaster = !twitchChannel.isEmpty() ? twitchChannel : twitchUsername;
            if (!broadcaster.isEmpty()) {
                helixClient.sendShoutoutToUser(broadcaster, user, [](bool success) {
                    qDebug() << "TwitchIntroGenerator: Shoutout REST API result:" << success;
                });
            }
        }
    }

    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);

    auto outputResult = [&](const QString &generatedText, const QString &displayName, bool success) {
        timeoutTimer.stop();
        if (format == "json") {
            QJsonObject resObj;
            resObj["status"] = success ? "success" : "error";
            resObj["username"] = user;
            resObj["displayName"] = displayName;
            resObj["text"] = generatedText;
            QJsonDocument outDoc(resObj);
            std::cout << outDoc.toJson(QJsonDocument::Compact).toStdString() << std::endl;
        } else {
            std::cout << generatedText.toUtf8().constData() << std::endl;
        }
        app.exit(success ? 0 : 1);
    };

    QObject::connect(&timeoutTimer, &QTimer::timeout, &app, [&]() {
        outputResult("紹介文生成タイムアウト", user, false);
    });
    timeoutTimer.start(timeoutMs);

    helixClient.fetchCreatorInfo(user, [&](const CreatorHelixInfo &info, bool success) {
        QString displayName = success && !info.displayName.trimmed().isEmpty() ? info.displayName : user;
        QString bio         = success ? info.description : "";
        QString game        = success ? info.gameName : "";
        QStringList recent  = success ? info.recentGames : QStringList{};
        QString title       = success ? info.title : "";
        QString sns         = success ? info.snsInfo : "";

        QString prompt;
        if (mode == "raid") {
            prompt = AIClientManager::buildRaidShoutoutPrompt(
                user, displayName, bio, game, recent, title, sns, length, tone);
        } else {
            prompt = AIClientManager::buildConversationShoutoutPrompt(
                user, displayName, bio, game, recent, title, sns, length, tone);
        }

        IAIClient *client = nullptr;
        if (aiProvider == "mistral" && !mistralKey.isEmpty()) {
            auto *c = new MistralAIClient(&app);
            c->setApiKey(mistralKey);
            if (!mistralModel.isEmpty()) c->setModel(mistralModel);
            client = c;
        } else if (aiProvider == "groq" && !groqKey.isEmpty()) {
            auto *c = new GroqAIClient(&app);
            c->setApiKey(groqKey);
            if (!groqModel.isEmpty()) c->setModel(groqModel);
            client = c;
        } else if (aiProvider == "gemini" && !geminiKey.isEmpty()) {
            auto *c = new GeminiAIClient(&app);
            c->setApiKey(geminiKey);
            if (!geminiModel.isEmpty()) c->setModel(geminiModel);
            client = c;
        } else if (aiProvider == "huggingface" && !hfKey.isEmpty()) {
            auto *c = new HuggingFaceAIClient(&app);
            c->setApiKey(hfKey);
            if (!hfModel.isEmpty()) c->setModel(hfModel);
            client = c;
        } else if (aiProvider == "openrouter" && !openrouterKey.isEmpty()) {
            auto *c = new OpenRouterAIClient(&app);
            c->setApiKey(openrouterKey);
            if (!openrouterModel.isEmpty()) c->setModel(openrouterModel);
            client = c;
        } else if (aiProvider == "sakura" && !sakuraKey.isEmpty()) {
            auto *c = new SakuraAIClient(&app);
            c->setApiKey(sakuraKey);
            if (!sakuraModel.isEmpty()) c->setModel(sakuraModel);
            client = c;
        } else {
            client = new DummyAIClient(&app);
        }

        QObject::connect(client, &IAIClient::requestFinished, &app, [=](const QString &response, bool reqSuccess, int httpCode) {
            Q_UNUSED(httpCode);
            if (reqSuccess && !response.trimmed().isEmpty()) {
                outputResult(response.trimmed(), displayName, true);
            } else {
                outputResult("紹介文の生成に失敗しました。", displayName, false);
            }
        });

        client->sendRequest(prompt, {}, "", "シャウトアウト紹介コメントを生成してください。");
    });

    return app.exec();
}
