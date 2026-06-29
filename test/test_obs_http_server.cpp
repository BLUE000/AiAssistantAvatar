#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QFile>
#include <QDir>
#include "obs/obs_http_server.h"

class ObsHttpServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // テスト用のダミードキュメントルートを準備 (pic ディレクトリがない場合に備えてダミーファイルを作る)
        QDir().mkpath("pic");
        QFile file("pic/avatar_obs.html");
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write("<html><body>Test Avatar OBS</body></html>");
            file.close();
        }
    }

    void TearDown() override {
        // テスト用ファイルのクリーンアップは通常行わなくても良いが、行っておく
    }
};

TEST_F(ObsHttpServerTest, StartStopTest) {
    ObsHttpServer server;
    // 競合しにくい高ポートを使用
    quint16 testPort = 59991;
    
    EXPECT_TRUE(server.start(testPort));
    EXPECT_TRUE(server.isListening());
    EXPECT_EQ(server.serverPort(), testPort);

    server.stop();
    EXPECT_FALSE(server.isListening());
}

TEST_F(ObsHttpServerTest, GetHtmlFileTest) {
    ObsHttpServer server;
    quint16 testPort = 59992;
    ASSERT_TRUE(server.start(testPort));

    QNetworkAccessManager manager;
    QUrl url(QString("http://127.0.0.1:%1/avatar_obs.html").arg(testPort));
    QNetworkRequest request(url);

    QNetworkReply *reply = manager.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    EXPECT_EQ(reply->error(), QNetworkReply::NoError);
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    EXPECT_EQ(statusCode, 200);

    QByteArray body = reply->readAll();
    EXPECT_TRUE(body.contains("Test Avatar OBS"));
    
    QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    EXPECT_TRUE(contentType.startsWith("text/html"));

    reply->deleteLater();
    server.stop();
}

TEST_F(ObsHttpServerTest, GetNonExistentFileTest) {
    ObsHttpServer server;
    quint16 testPort = 59993;
    ASSERT_TRUE(server.start(testPort));

    QNetworkAccessManager manager;
    QUrl url(QString("http://127.0.0.1:%1/not_found_file.html").arg(testPort));
    QNetworkRequest request(url);

    QNetworkReply *reply = manager.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    // 404エラーが返ることを期待
    EXPECT_EQ(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 404);

    reply->deleteLater();
    server.stop();
}

TEST_F(ObsHttpServerTest, DirectoryTraversalBlockTest) {
    ObsHttpServer server;
    quint16 testPort = 59994;
    ASSERT_TRUE(server.start(testPort));

    QNetworkAccessManager manager;
    // 親ディレクトリへのトラバーサルリクエスト
    QUrl url(QString("http://127.0.0.1:%1/../CMakeLists.txt").arg(testPort));
    QNetworkRequest request(url);

    QNetworkReply *reply = manager.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    // 403 Forbidden または 400 Bad Request 等が返ることを期待 (トラバーサルブロック)
    int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    EXPECT_TRUE(code == 403 || code == 400 || code == 404);

    reply->deleteLater();
    server.stop();
}
