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
    if (rootDir.exists("FishEatCatSkin")) {
        m_documentRoot = rootDir.filePath("FishEatCatSkin");
    } else {
        m_documentRoot = rootDir.canonicalPath();
    }
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

void ObsHttpServer::setDocumentRoot(const QString &path) {
    if (path.isEmpty()) return;
    QDir dir(path);
    if (dir.exists()) {
        m_documentRoot = dir.canonicalPath();
        qDebug() << "ObsHttpServer: Updated Document root to:" << m_documentRoot;
    }
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

    // パーセントエンコーディングのデコードとクエリ文字列の除去
    QString decodedPath = QUrl::fromPercentEncoding(rawPath.toUtf8());
    QString pathOnly = decodedPath.split('?').first().toLower();

    // /stt または /stt_input エンドポイントの判定
    if (pathOnly == "/stt" || pathOnly == "/stt_input") {
        QUrl url("http://localhost" + rawPath);
        QUrlQuery query(url);
        QString textParam = query.queryItemValue("text");

        // POST ボディのパース (JSON またはプレーンテキスト)
        if (textParam.isEmpty() && method == "POST") {
            int bodyIdx = requestStr.indexOf("\r\n\r\n");
            if (bodyIdx != -1) {
                QString body = requestStr.mid(bodyIdx + 4);
                QJsonDocument doc = QJsonDocument::fromJson(body.toUtf8());
                if (doc.isObject()) {
                    textParam = doc.object().value("text").toString();
                } else if (!body.trimmed().isEmpty()) {
                    textParam = body.trimmed();
                }
            }
        }

        if (!textParam.isEmpty()) {
            qDebug() << "ObsHttpServer: Received STT text via HTTP:" << textParam;
            emit sttTextReceived(textParam);
            QJsonObject resObj;
            resObj["status"] = "success";
            resObj["message"] = "STT text received and routed to AI";
            resObj["text"] = textParam;
            sendResponse(socket, 200, "OK", QJsonDocument(resObj).toJson(), "application/json");
            return;
        }

        // テキスト指定がない GET アクセスの場合、WebSTT 音声入力 ＆ ウェイクワード常時監視 Web 画面 (HTML) を返却
        QString html = R"rawhtml(<!DOCTYPE html>
<html lang="ja">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>AiAssistantAvatar - WebSTT Voice Input</title>
  <style>
    body {
      margin: 0; padding: 20px;
      background: #0f172a; color: #f8fafc;
      font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
      display: flex; flex-direction: column; align-items: center; justify-content: center;
      min-height: 90vh;
    }
    .card {
      background: #1e293b; border: 1px solid #334155; border-radius: 16px;
      padding: 30px; width: 90%; max-width: 480px;
      box-shadow: 0 10px 25px rgba(0,0,0,0.5); text-align: center;
    }
    h2 { margin-top: 0; color: #38bdf8; font-size: 24px; }
    .status-pill {
      display: inline-block; padding: 6px 14px; border-radius: 20px;
      font-size: 14px; font-weight: bold; margin-bottom: 20px;
    }
    .active { background: #065f46; color: #34d399; }
    .inactive { background: #881337; color: #fda4af; }
    .mic-btn {
      width: 100px; height: 100px; border-radius: 50%; border: none;
      background: #0284c7; color: white; font-size: 40px; cursor: pointer;
      box-shadow: 0 0 20px rgba(2, 132, 199, 0.5); transition: all 0.3s ease;
      margin: 15px 0;
    }
    .mic-btn.listening {
      background: #ef4444; box-shadow: 0 0 30px rgba(239, 68, 68, 0.8);
      animation: pulse 1.5s infinite;
    }
    @keyframes pulse {
      0% { transform: scale(1); }
      50% { transform: scale(1.08); }
      100% { transform: scale(1); }
    }
    .text-box {
      background: #0f172a; border: 1px solid #475569; border-radius: 8px;
      padding: 15px; min-height: 80px; margin-top: 15px; text-align: left;
      font-size: 16px; color: #e2e8f0; word-break: break-all;
    }
    .wakeword-info {
      font-size: 12px; color: #94a3b8; margin-top: 10px;
    }
  </style>
</head>
<body>
  <div class="card">
    <h2>🎙️ WebSTT 音声入力 UI</h2>
    <div id="statusPill" class="status-pill inactive">マイク待機中</div>
    <div>
      <button id="micBtn" class="mic-btn">🎤</button>
    </div>
    <div class="wakeword-info">
      マイクに向かって喋ると自動で本アプリへ音声テキストが送信されます。
    </div>
    <div id="transcriptBox" class="text-box">ここに音声認識されたテキストが表示されます...</div>
  </div>

  <script>
    const micBtn = document.getElementById('micBtn');
    const statusPill = document.getElementById('statusPill');
    const transcriptBox = document.getElementById('transcriptBox');

    const SpeechRecognition = window.SpeechRecognition || window.webkitSpeechRecognition;
    if (!SpeechRecognition) {
      statusPill.textContent = "お使いのブラウザは音声認識非対応です";
      statusPill.className = "status-pill inactive";
    } else {
      const recognition = new SpeechRecognition();
      recognition.continuous = true;
      recognition.interimResults = true;
      recognition.lang = 'ja-JP';

      let isListening = false;

      function startListening() {
        try {
          recognition.start();
          isListening = true;
          micBtn.classList.add('listening');
          statusPill.textContent = "🎙️ 音声常時監視中...";
          statusPill.className = "status-pill active";
        } catch (e) { console.error(e); }
      }

      function stopListening() {
        recognition.stop();
        isListening = false;
        micBtn.classList.remove('listening');
        statusPill.textContent = "マイク停止中";
        statusPill.className = "status-pill inactive";
      }

      micBtn.addEventListener('click', () => {
        if (isListening) stopListening();
        else startListening();
      });

      recognition.onresult = (event) => {
        let interimTranscript = '';
        let finalTranscript = '';

        for (let i = event.resultIndex; i < event.results.length; ++i) {
          if (event.results[i].isFinal) {
            finalTranscript += event.results[i][0].transcript;
          } else {
            interimTranscript += event.results[i][0].transcript;
          }
        }

        const displayText = finalTranscript || interimTranscript;
        if (displayText) {
          transcriptBox.textContent = displayText;
        }

        if (finalTranscript.trim()) {
          sendTextToApp(finalTranscript.trim());
        }
      };

      recognition.onend = () => {
        if (isListening) {
          try { recognition.start(); } catch (e) {}
        }
      };

      startListening();
    }

    function sendTextToApp(text) {
      statusPill.textContent = "📤 アプリへ送信中...";
      fetch(`/stt?text=${encodeURIComponent(text)}`)
        .then(res => res.json())
        .then(data => {
          console.log("Send result:", data);
          statusPill.textContent = "🟢 送信完了！話しかけてください";
          statusPill.className = "status-pill active";
        })
        .catch(err => {
          console.error("Send error:", err);
        });
    }
  </script>
</body>
</html>)rawhtml";

        sendResponse(socket, 200, "OK", html.toUtf8(), "text/html; charset=utf-8");
        return;
    }

    // /ui_text または /text_overlay.html エンドポイントの判定 (F-31: UI応答専用Webテキスト表示)
    if (pathOnly == "/ui_text" || pathOnly == "/text_overlay.html") {
        QString html = R"rawhtml(<!DOCTYPE html>
<html lang="ja">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>AiAssistantAvatar - UI Text Viewer</title>
  <style>
    body {
      margin: 0; padding: 20px;
      background: #0f172a; color: #f8fafc;
      font-family: 'Segoe UI', 'Meiryo', sans-serif;
      display: flex; flex-direction: column; align-items: center; justify-content: center;
      min-height: 90vh;
    }
    .container {
      background: #1e293b; border: 1px solid #334155; border-radius: 16px;
      padding: 30px; width: 90%; max-width: 800px;
      box-shadow: 0 10px 25px rgba(0,0,0,0.5);
    }
    .header {
      font-size: 16px; color: #38bdf8; font-weight: bold;
      border-bottom: 1px solid #334155; padding-bottom: 10px; margin-bottom: 20px;
      display: flex; justify-content: space-between; align-items: center;
    }
    .status-badge {
      font-size: 12px; padding: 4px 10px; border-radius: 12px; background: #065f46; color: #34d399;
    }
    .text-content {
      font-size: 24px; line-height: 1.6; color: #f1f5f9; white-space: pre-wrap; word-break: break-all;
      min-height: 150px;
    }
    .placeholder { color: #64748b; font-style: italic; }
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <span>💡 AI アシスタント - 応答テキスト表示</span>
      <span id="status" class="status-badge">接続待機中...</span>
    </div>
    <div id="textContent" class="text-content placeholder">AIへの質問・音声入力の応答がここに表示されます...</div>
  </div>

  <script>
    const statusEl = document.getElementById('status');
    const textContentEl = document.getElementById('textContent');

    function connectWS() {
      const host = window.location.hostname || 'localhost';
      const ws = new WebSocket(`ws://${host}:58081`);

      ws.onopen = () => {
        statusEl.textContent = '🟢 接続中';
        statusEl.style.background = '#065f46';
        statusEl.style.color = '#34d399';
      };

      ws.onmessage = (event) => {
        try {
          const data = JSON.parse(event.data);
          if (data.type === 'UIResponse' || (data.type === 'AIResponse' && data.source === 'UI')) {
            if (data.text) {
              textContentEl.classList.remove('placeholder');
              textContentEl.textContent = data.text;
            }
          }
        } catch (e) { console.error('JSON parse error:', e); }
      };

      ws.onclose = () => {
        statusEl.textContent = '🔴 切断（再接続中...）';
        statusEl.style.background = '#881337';
        statusEl.style.color = '#fda4af';
        setTimeout(connectWS, 3000);
      };
    }

    connectWS();
  </script>
</body>
</html>)rawhtml";

        sendResponse(socket, 200, "OK", html.toUtf8(), "text/html; charset=utf-8");
        return;
    }


    if (method != "GET" && method != "HEAD") {
        sendErrorResponse(socket, 405, "Method Not Allowed", "Only GET and HEAD methods are supported");
        return;
    }

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
    QString relativePath = decodedPath;
    if (relativePath.startsWith("/")) {
        relativePath = relativePath.mid(1);
    }
    
    // 1. アクティブなドキュメントルート (選択中スキンディレクトリ) から探索
    QDir rootDir(m_documentRoot);
    QString targetFilePath = rootDir.filePath(relativePath);
    QFileInfo fileInfo(targetFilePath);

    // 2. 存在しない場合、親ディレクトリ (pic ディレクトリ直下) や別スキンでフォールバック探索
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        QDir parentPicDir(rootDir);
        if (parentPicDir.dirName() != "pic") {
            parentPicDir.cdUp();
        }

        // (A) pic/ ディレクトリ直下で探索
        QString fallbackPath = parentPicDir.filePath(relativePath);
        QFileInfo fallbackInfo(fallbackPath);
        if (fallbackInfo.exists() && fallbackInfo.isFile()) {
            fileInfo = fallbackInfo;
        } else {
            // (B) pic/ 配下の全サブディレクトリ（全スキンフォルダ）を探索
            QFileInfoList subDirs = parentPicDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo &subDir : subDirs) {
                QDir skinDir(subDir.absoluteFilePath());
                QString skinFilePath = skinDir.filePath(relativePath);
                QFileInfo skinFileInfo(skinFilePath);
                if (skinFileInfo.exists() && skinFileInfo.isFile()) {
                    fileInfo = skinFileInfo;
                    break;
                }
            }
        }
    }

    QString canonicalPath = fileInfo.canonicalFilePath();

    // ファイルが存在しない場合の厳密なチェック
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        sendErrorResponse(socket, 404, "Not Found", "File not found");
        return;
    }

    sendFileResponse(socket, canonicalPath);
}

#include <QImage>
#include <QBuffer>
#include <QQueue>

static QByteArray processImageTransparency(const QString &filePath, int tx = 0, int ty = 0) {
    QImage image(filePath);
    if (image.isNull()) return QByteArray();

    image = image.convertToFormat(QImage::Format_ARGB32);
    int width  = image.width();
    int height = image.height();

    if (tx < 0 || tx >= width || ty < 0 || ty >= height) {
        tx = 0; ty = 0;
    }

    // 指定座標 (tx, ty) のピクセル色を基準透過色として取得
    QRgb targetColor = image.pixel(tx, ty);

    int tR = qRed(targetColor);
    int tG = qGreen(targetColor);
    int tB = qBlue(targetColor);

    const int kTolerance = 40;
    auto isSimilar = [&](QRgb c) -> bool {
        return qAbs(qRed(c)   - tR) <= kTolerance &&
               qAbs(qGreen(c) - tG) <= kTolerance &&
               qAbs(qBlue(c)  - tB) <= kTolerance;
    };

    QList<QPoint> seedPoints = {
        QPoint(tx, ty),
        QPoint(0, 0),
        QPoint(width - 1, 0),
        QPoint(0, height - 1),
        QPoint(width - 1, height - 1)
    };

    QQueue<QPoint> queue;
    QVector<QVector<bool>> visited(width, QVector<bool>(height, false));

    for (const QPoint &pt : seedPoints) {
        if (isSimilar(image.pixel(pt))) {
            queue.enqueue(pt);
            visited[pt.x()][pt.y()] = true;
        }
    }

    static const int dx[] = { 1, -1, 0, 0 };
    static const int dy[] = { 0, 0, 1, -1 };

    while (!queue.isEmpty()) {
        QPoint pt = queue.dequeue();
        image.setPixel(pt, qRgba(0, 0, 0, 0));

        for (int i = 0; i < 4; ++i) {
            int nx = pt.x() + dx[i];
            int ny = pt.y() + dy[i];
            if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                if (!visited[nx][ny]) {
                    visited[nx][ny] = true;
                    if (isSimilar(image.pixel(nx, ny))) {
                        queue.enqueue(QPoint(nx, ny));
                    }
                }
            }
        }
    }

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

void ObsHttpServer::sendFileResponse(QTcpSocket *socket, const QString &filePath) {
    // MIMEタイプの決定
    QMimeDatabase mimeDb;
    QMimeType mime = mimeDb.mimeTypeForFile(filePath);
    QString contentType = mime.name();
    if (contentType.isEmpty()) {
        contentType = "application/octet-stream";
    }

    QByteArray body;
    if (filePath.endsWith(".png", Qt::CaseInsensitive) || filePath.endsWith(".jpg", Qt::CaseInsensitive)) {
        body = processImageTransparency(filePath);
    }

    if (body.isEmpty()) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            sendErrorResponse(socket, 500, "Internal Server Error", "Failed to open file");
            return;
        }
        body = file.readAll();
        file.close();
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
