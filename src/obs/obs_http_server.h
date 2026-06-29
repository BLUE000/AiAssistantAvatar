#pragma once
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QUrlQuery>
#include <QMimeDatabase>
#include <QMimeType>
#include <QJsonObject>
#include <QJsonDocument>

class ObsHttpServer : public QObject {
    Q_OBJECT
public:
    explicit ObsHttpServer(QObject *parent = nullptr);
    ~ObsHttpServer();

    bool start(quint16 port);
    void stop();
    bool isListening() const;
    quint16 serverPort() const;

private slots:
    void handleNewConnection();

private:
    QTcpServer *m_server = nullptr;
    QString m_documentRoot;

    void handleRequest(QTcpSocket *socket, const QString &requestStr);
    void sendResponse(QTcpSocket *socket, int statusCode, const QString &statusText, 
                      const QByteArray &body, const QString &contentType = "text/plain");
    void sendFileResponse(QTcpSocket *socket, const QString &filePath);
    void sendErrorResponse(QTcpSocket *socket, int statusCode, const QString &statusText, const QString &message);
};
