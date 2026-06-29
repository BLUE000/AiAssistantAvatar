#include "obs_http_server.h"
#include <QCoreApplication>
#include <QTextStream>
#include <QDateTime>
#include <QDebug>

ObsHttpServer::ObsHttpServer(QObject *parent)
    : QObject(parent)
{
    // ドキュメントルート (pic ディレクトリ) の特定
    m_documentRoot = "pic";
#ifdef PROJECT_SOURCE_DIR
    if (!QDir(m_documentRoot).exists()) {
        m_documentRoot = QString(PROJECT_SOURCE_DIR) + "/pic";
    }
#endif
    if (!QDir(m_documentRoot).exists()) {
        m_documentRoot = QCoreApplication::applicationDirPath() + "/pic";
    }

    QDir rootDir(m_documentRoot);
    m_documentRoot = rootDir.canonicalPath();
    qDebug() << "ObsHttpServer: Document root set to:" << m_documentRoot;
}

ObsHttpServer::~ObsHttpServer() {
    stop();
}

bool ObsHttpServer::start(quint16 port) {
    stop();

    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &ObsHttpServer::handleNewConnection);

    // QHostAddress::Any で外部からの接続も受け付ける
    if (!m_server->listen(QHostAddress::Any, port)) {
        qCritical() << "ObsHttpServer: Failed to start HTTP server on port" << port;
        stop();
        return false;
    }

    qDebug() << "ObsHttpServer: Listening on port" << port;
    return true;
}

void ObsHttpServer::stop() {
    if (m_server) {
        if (m_server->isListening()) {
            m_server->close();
            qDebug() << "ObsHttpServer: Stopped listening.";
        }
        delete m_server;
        m_server = nullptr;
    }
}

bool ObsHttpServer::isListening() const {
    return m_server && m_server->isListening();
}

quint16 ObsHttpServer::serverPort() const {
    return m_server ? m_server->serverPort() : 0;
}

void ObsHttpServer::handleNewConnection() {
    if (!m_server) return;

    QTcpSocket *socket = m_server->nextPendingConnection();
    if (!socket) return;

    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
        QByteArray requestData = socket->readAll();
        QString requestStr = QString::fromUtf8(requestData);

        handleRequest(socket, requestStr);
    });

    // 切断時のクリーンアップ
    connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
}

void ObsHttpServer::handleRequest(QTcpSocket *socket, const QString &requestStr) {
    QStringList lines = requestStr.split("\r\n");
    if (lines.isEmpty()) {
        sendErrorResponse(socket, 400, "Bad Request", "Empty request");
        return;
    }

    QString firstLine = lines.first();
    QStringList parts = firstLine.split(" ");
    if (parts.size() < 2) {
        sendErrorResponse(socket, 400, "Bad Request", "Invalid request line");
        return;
    }

    QString method = parts.at(0);
    QString rawPath = parts.at(1);

    if (method != "GET" && method != "HEAD") {
        sendErrorResponse(socket, 405, "Method Not Allowed", "Only GET and HEAD methods are supported");
        return;
    }

    // パーセントエンコーディングのデコードとクエリ文字列の除去
    QString decodedPath = QUrl::fromPercentEncoding(rawPath.toUtf8());
    decodedPath = decodedPath.split('?').first();

    // デフォルトドキュメント
    if (decodedPath == "/" || decodedPath.isEmpty()) {
        decodedPath = "/avatar_obs.html";
    }

    // ディレクトリトラバーサルの簡単な文字列チェック
    if (decodedPath.contains("..")) {
        sendErrorResponse(socket, 403, "Forbidden", "Access denied (traversal attempt)");
        return;
    }

    // ドキュメントルートと結合して絶対正規化パスを取得
    QDir rootDir(m_documentRoot);
    QString relativePath = decodedPath;
    if (relativePath.startsWith("/")) {
        relativePath = relativePath.mid(1);
    }
    
    QString targetFilePath = rootDir.filePath(relativePath);
    QFileInfo fileInfo(targetFilePath);
    QString canonicalPath = fileInfo.canonicalFilePath();

    // ファイルが存在しない、または親ディレクトリにトラバースしているかの厳密なチェック
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        sendErrorResponse(socket, 404, "Not Found", "File not found");
        return;
    }

    if (!canonicalPath.startsWith(m_documentRoot)) {
        sendErrorResponse(socket, 403, "Forbidden", "Access denied");
        return;
    }

    sendFileResponse(socket, canonicalPath);
}

void ObsHttpServer::sendFileResponse(QTcpSocket *socket, const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        sendErrorResponse(socket, 500, "Internal Server Error", "Failed to open file");
        return;
    }

    QByteArray body = file.readAll();
    file.close();

    // MIMEタイプの決定
    QMimeDatabase mimeDb;
    QMimeType mime = mimeDb.mimeTypeForFile(filePath);
    QString contentType = mime.name();
    if (contentType.isEmpty()) {
        contentType = "application/octet-stream";
    }

    sendResponse(socket, 200, "OK", body, contentType);
}

void ObsHttpServer::sendResponse(QTcpSocket *socket, int statusCode, const QString &statusText, 
                                 const QByteArray &body, const QString &contentType) 
{
    QTextStream response(socket);
    response.setEncoding(QStringConverter::Utf8);
    
    response << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n";
    response << "Server: ObsHttpServer/1.0\r\n";
    response << "Date: " << QDateTime::currentDateTimeUtc().toString("ddd, dd MMM yyyy hh:mm:ss t") << " GMT\r\n";
    response << "Content-Type: " << contentType << "; charset=utf-8\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    response << "Access-Control-Allow-Origin: *\r\n"; // CORS許可ヘッダー
    response << "Connection: close\r\n\r\n";
    response.flush();

    socket->write(body);
    socket->flush();
    socket->disconnectFromHost();
}

void ObsHttpServer::sendErrorResponse(QTcpSocket *socket, int statusCode, const QString &statusText, const QString &message) {
    QJsonObject json;
    json["status"] = statusCode;
    json["error"] = statusText;
    json["message"] = message;
    QJsonDocument doc(json);
    QByteArray body = doc.toJson(QJsonDocument::Compact);

    sendResponse(socket, statusCode, statusText, body, "application/json");
}
