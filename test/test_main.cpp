#include <gtest/gtest.h>
#include <QApplication>
#include <iostream>

void quietMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QByteArray localMsg = msg.toLocal8Bit();
    switch (type) {
    case QtDebugMsg:
        fprintf(stderr, "Debug: %s\n", localMsg.constData());
        break;
    case QtInfoMsg:
        fprintf(stderr, "Info: %s\n", localMsg.constData());
        break;
    case QtWarningMsg:
        fprintf(stderr, "Warning: %s\n", localMsg.constData());
        break;
    case QtCriticalMsg:
        fprintf(stderr, "Critical: %s\n", localMsg.constData());
        break;
    case QtFatalMsg:
        fprintf(stderr, "Fatal: %s\n", localMsg.constData());
    }
    fflush(stderr);
}

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

int main(int argc, char* argv[])
{
    qInstallMessageHandler(quietMessageHandler);
    QApplication app(argc, argv);

    // テスト実行環境専用の Config/local_settings.json を準備 (テスト隔離)
    QString appDir = QCoreApplication::applicationDirPath();
    QStringList targetPaths = {
        appDir + "/Config/local_settings.json",
        appDir + "/../Config/local_settings.json",
        "Config/local_settings.json"
    };

    QJsonObject testObj;
    testObj["ai_provider"] = "dummy";
    testObj["mistral_api_key"] = "test_api_key_from_test";
    testObj["trans_cipher_key"] = "AiAssistantAvatar";
    testObj["twitch_channel"] = "YOUR_CHANNEL_NAME";
    testObj["twitch_client_id"] = "test_client_id";
    testObj["twitch_port"] = 48080;
    testObj["twitch_wakeword"] = "AI";
    testObj["twitch_wakeword_mode"] = "contains";
    testObj["blacklist_enabled"] = true;
    testObj["manager_ai_enabled"] = false;
    testObj["manager_ai_provider"] = "dummy";

    for (const QString &path : targetPaths) {
        QFileInfo fi(path);
        QDir().mkpath(fi.absolutePath());
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(QJsonDocument(testObj).toJson());
            file.close();
        }
    }

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
