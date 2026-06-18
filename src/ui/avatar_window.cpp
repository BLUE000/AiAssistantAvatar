#include "avatar_window.h"
#include "balloon_widget.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMouseEvent>
#include <QMenu>
#include <QInputDialog>
#include <QGuiApplication>
#include <QScreen>
#include <QQueue>
#include <QDir>
#include <QDebug>
#include <QFileDialog>
#include <QRandomGenerator>

AvatarWindow::AvatarWindow(QWidget *parent)
    : QMainWindow(parent), m_currentState("idle") 
{
    // 背景透過・枠なしウィンドウの設定
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::SubWindow);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);

    m_avatarLabel = new QLabel(this);
    m_avatarLabel->setAlignment(Qt::AlignCenter);

    // デフォルト目標位置（画面右下付近）の設定
    QScreen *primaryScreen = QGuiApplication::primaryScreen();
    QRect screenGeometry = primaryScreen->geometry();
    m_desktopTargetPos = QPoint(screenGeometry.width() - 200, screenGeometry.height() - 250);

    // バルーンの生成
    m_balloon = new BalloonWidget(this);
    m_balloon->hide();

    loadSettings();
    processAndCacheImages();
    updateAvatarDisplay("idle");

    // フロント画像ランダム切り替えタイマーの設定
    if (!m_frontVariants.isEmpty()) {
        m_variantTimer = new QTimer(this);
        connect(m_variantTimer, &QTimer::timeout, this, &AvatarWindow::switchToNextVariant);
        m_variantTimer->start(m_frontVariants.intervalMs);
        m_isFrontVariantMode = true;
        // 初期表示を即座にバリアント画像で上書き
        switchToNextVariant();
    }
}

AvatarWindow::~AvatarWindow() {
}

void AvatarWindow::loadSettings() {
    QString configPath = "pic/avatar_settings.json";
    
    // ディレクトリ作成
    QDir().mkpath("pic");

    // 設定ファイルが無い場合は初期デフォルトを作成
    if (!QFile::exists(configPath)) {
        QJsonObject defaultSettings;
        
        auto createSetting = [](const QString &file) {
            QJsonObject obj;
            obj["file"] = file;
            obj["anchorX"] = 100;
            obj["anchorY"] = 100;
            obj["transparentX"] = 0;
            obj["transparentY"] = 0;
            return obj;
        };

        defaultSettings["idle"] = createSetting("idle.png");
        defaultSettings["listening"] = createSetting("listening.png");
        defaultSettings["thinking"] = createSetting("thinking.png");
        defaultSettings["speaking"] = createSetting("speaking.png");

        QJsonDocument doc(defaultSettings);
        QFile file(configPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(doc.toJson());
            file.close();
            qDebug() << "Created default settings file:" << configPath;
        }
    }

    // 読み込み
    QFile file(configPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QByteArray data = file.readAll();
        file.close();
        
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject obj = doc.object();
            QStringList states = {"idle", "listening", "thinking", "speaking"};
            for (const QString &state : states) {
                if (obj.contains(state) && obj[state].isObject()) {
                    QJsonObject stateObj = obj[state].toObject();
                    ImageSetting setting;
                    setting.filePath = "pic/" + stateObj["file"].toString();
                    setting.anchorX = stateObj["anchorX"].toInt(100);
                    setting.anchorY = stateObj["anchorY"].toInt(100);
                    setting.transparentX = stateObj["transparentX"].toInt(0);
                    setting.transparentY = stateObj["transparentY"].toInt(0);
                    m_imageSettings[state] = setting;
                }
            }

            // フロントバリアント設定の読み込み
            if (obj.contains("front_variants") && obj["front_variants"].isObject()) {
                QJsonObject varObj = obj["front_variants"].toObject();
                m_frontVariants.anchorX      = varObj["anchorX"].toInt(100);
                m_frontVariants.anchorY      = varObj["anchorY"].toInt(100);
                m_frontVariants.transparentX = varObj["transparentX"].toInt(0);
                m_frontVariants.transparentY = varObj["transparentY"].toInt(0);
                m_frontVariants.intervalMs   = varObj["interval_ms"].toInt(5000);

                if (varObj.contains("files") && varObj["files"].isArray()) {
                    QJsonArray filesArr = varObj["files"].toArray();
                    for (const QJsonValue &v : filesArr) {
                        // 各エントリはオブジェクト形式：{ "file": "Front01.png", "weight": 1 }
                        if (v.isObject()) {
                            QJsonObject entry = v.toObject();
                            FrontVariantEntry e;
                            e.filePath = "pic/" + entry["file"].toString();
                            e.weight   = entry["weight"].toInt(1);
                            if (e.weight < 1) e.weight = 1; // 最小重みを保証
                            m_frontVariants.entries.append(e);
                        }
                    }
                }
            }
        }
    }
}

QPixmap AvatarWindow::applyTransparency(const QString &filePath, int tx, int ty) {
    QImage image(filePath);
    if (image.isNull()) {
        qWarning() << "Failed to load image:" << filePath;
        // 代替として空のダミーイメージを作成
        QImage dummy(200, 200, QImage::Format_ARGB32);
        dummy.fill(Qt::transparent);
        return QPixmap::fromImage(dummy);
    }

    image = image.convertToFormat(QImage::Format_ARGB32);
    int width = image.width();
    int height = image.height();

    // 指定座標 (tx, ty) が画像範囲内か確認
    if (tx < 0 || tx >= width || ty < 0 || ty >= height) {
        tx = 0;
        ty = 0;
    }

    QRgb targetColor = image.pixel(tx, ty);
    QColor transColor(0, 0, 0, 0);

    // BFSによる Flood Fill 透過処理
    QQueue<QPoint> queue;
    queue.enqueue(QPoint(tx, ty));

    QVector<QVector<bool>> visited(width, QVector<bool>(height, false));
    visited[tx][ty] = true;

    const int dx[] = {0, 0, 1, -1};
    const int dy[] = {1, -1, 0, 0};

    while (!queue.isEmpty()) {
        QPoint p = queue.dequeue();
        
        if (image.pixel(p) == targetColor) {
            image.setPixelColor(p, transColor);
            
            for (int i = 0; i < 4; ++i) {
                int nx = p.x() + dx[i];
                int ny = p.y() + dy[i];
                
                if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                    if (!visited[nx][ny]) {
                        visited[nx][ny] = true;
                        queue.enqueue(QPoint(nx, ny));
                    }
                }
            }
        }
    }
    return QPixmap::fromImage(image);
}

void AvatarWindow::processAndCacheImages() {
    for (auto it = m_imageSettings.begin(); it != m_imageSettings.end(); ++it) {
        QString state = it.key();
        ImageSetting setting = it.value();
        
        // 画像が無い場合はデモ用プレースホルダを作成
        if (!QFile::exists(setting.filePath)) {
            QImage dummy(200, 200, QImage::Format_ARGB32);
            dummy.fill(Qt::black); // 黒一色
            dummy.save(setting.filePath);
            qDebug() << "Generated dummy image:" << setting.filePath;
        }

        QPixmap pixmap = applyTransparency(setting.filePath, setting.transparentX, setting.transparentY);
        m_pixmapCache[state] = pixmap;
    }

    // フロントバリアント画像の事前キャッシュ + 累積重みテーブル構築
    m_frontPixmapCache.clear();
    m_weightCumulative.clear();
    int cumulative = 0;
    for (const FrontVariantEntry &entry : m_frontVariants.entries) {
        if (QFile::exists(entry.filePath)) {
            QPixmap px = applyTransparency(entry.filePath,
                                           m_frontVariants.transparentX,
                                           m_frontVariants.transparentY);
            m_frontPixmapCache.append(px);
            cumulative += entry.weight;
            m_weightCumulative.append(cumulative);
            qDebug() << "Cached front variant:" << entry.filePath << "weight:" << entry.weight;
        } else {
            qWarning() << "Front variant not found:" << entry.filePath;
        }
    }
}

void AvatarWindow::updateAvatarDisplay(const QString &state) {
    if (!m_pixmapCache.contains(state)) return;
    m_currentState = state;
    m_isFrontVariantMode = false; // 状態切り替え時はバリアントモードを一時停止
    updateWindowPosition();
}

void AvatarWindow::switchToNextVariant() {
    if (m_frontPixmapCache.isEmpty() || m_weightCumulative.isEmpty()) return;

    // 累積重みテーブルを使った重み付きランダム選択
    int totalWeight = m_weightCumulative.last();
    int count       = m_frontPixmapCache.size();
    int nextIndex   = m_currentFrontIndex;

    // 重み合計が1以上の場合のみアイドル防止ループ
    int maxTry = count * 10;
    for (int i = 0; i < maxTry; ++i) {
        int rnd = static_cast<int>(QRandomGenerator::global()->bounded(totalWeight));
        // 下界バイナリサーチでインデックスを決定
        int idx = 0;
        for (int j = 0; j < m_weightCumulative.size(); ++j) {
            if (rnd < m_weightCumulative[j]) { idx = j; break; }
        }
        if (count <= 1 || idx != m_currentFrontIndex) {
            nextIndex = idx;
            break;
        }
    }
    m_currentFrontIndex = nextIndex;
    m_isFrontVariantMode = true;

    // バリアント画像をラベルに適用
    QPixmap px = m_frontPixmapCache[m_currentFrontIndex];
    this->resize(px.size());
    m_avatarLabel->resize(px.size());
    m_avatarLabel->setPixmap(px);

    int newX = m_desktopTargetPos.x() - m_frontVariants.anchorX;
    int newY = m_desktopTargetPos.y() - m_frontVariants.anchorY;
    this->move(newX, newY);

    if (m_balloon && m_balloon->isVisible()) {
        m_balloon->move(newX + px.width() - 40, newY - 60);
    }
}

void AvatarWindow::updateWindowPosition() {
    if (!m_pixmapCache.contains(m_currentState) || !m_imageSettings.contains(m_currentState)) return;
    
    QPixmap currentPixmap = m_pixmapCache[m_currentState];
    ImageSetting setting = m_imageSettings[m_currentState];

    this->resize(currentPixmap.size());
    m_avatarLabel->resize(currentPixmap.size());
    m_avatarLabel->setPixmap(currentPixmap);

    // アンカー基準による位置ズレ補正移動
    int newX = m_desktopTargetPos.x() - setting.anchorX;
    int newY = m_desktopTargetPos.y() - setting.anchorY;
    this->move(newX, newY);

    // バルーン位置の追従 (アバターの右上付近に表示)
    if (m_balloon && m_balloon->isVisible()) {
        m_balloon->move(newX + currentPixmap.width() - 40, newY - 60);
    }
}

void AvatarWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
}

void AvatarWindow::mouseMoveEvent(QMouseEvent *event) {
    if (event->buttons() & Qt::LeftButton) {
        QPoint newPos = event->globalPosition().toPoint() - m_dragPosition;
        
        // ドラッグ時はアンカー位置から目標位置を逆算して保存
        ImageSetting setting = m_imageSettings[m_currentState];
        m_desktopTargetPos = QPoint(newPos.x() + setting.anchorX, newPos.y() + setting.anchorY);
        
        move(newPos);
        
        // バルーンの追従
        if (m_balloon && m_balloon->isVisible()) {
            m_balloon->move(newPos.x() + width() - 40, newPos.y() - 60);
        }
        event->accept();
    }
}

void AvatarWindow::contextMenuEvent(QContextMenuEvent *event) {
    QMenu menu(this);
    
    QAction *actDirectInput = menu.addAction("直接テキスト入力");
    QAction *actVoiceInput = menu.addAction("音声入力開始(STT)");
    QAction *actResetHistory = menu.addAction("会話履歴をクリアして要約");
    QAction *actImportHistory = menu.addAction("会話履歴をインポート...");
    QAction *actExportHistory = menu.addAction("会話履歴をエクスポート...");
    menu.addSeparator();
    QAction *actQuit = menu.addAction("終了");

    QAction *selected = menu.exec(event->globalPos());
    
    if (selected == actDirectInput) {
        bool ok;
        QString text = QInputDialog::getText(this, "AIへの直接命令", 
                                             "メッセージを入力してください:", 
                                             QLineEdit::Normal, "", &ok);
        if (ok && !text.isEmpty()) {
            emit directInputSubmitted(text);
        }
    } else if (selected == actVoiceInput) {
        emit startSTTRequested();
    } else if (selected == actResetHistory) {
        emit resetSessionRequested();
    } else if (selected == actImportHistory) {
        QString filePath = QFileDialog::getOpenFileName(this, "会話履歴のインポート", "log", "Encrypted Backups (*.enc)");
        if (!filePath.isEmpty()) {
            emit importSessionRequested(filePath);
        }
    } else if (selected == actExportHistory) {
        QString encPath = QFileDialog::getOpenFileName(this, "エクスポート元（暗号ファイル）の選択", "log", "Encrypted Backups (*.enc)");
        if (!encPath.isEmpty()) {
            QString txtPath = QFileDialog::getSaveFileName(this, "エクスポート先（テキストファイル）の選択", "log/decrypted_history.txt", "Text Files (*.txt)");
            if (!txtPath.isEmpty()) {
                emit exportSessionRequested(encPath, txtPath);
            }
        }
    } else if (selected == actQuit) {
        close();
    }
}

void AvatarWindow::on_notify_events(const AppEvent &event) {
    qDebug() << "AvatarWindow received event. Type:" << static_cast<int>(event.type) << "Text:" << event.text;

    switch (event.type) {
        case EventType::VoiceInputStarted:
            updateAvatarDisplay("listening");
            m_balloon->showText("マイクの音声を聞いています...");
            break;
        
        case EventType::VoiceInputCompleted:
            updateAvatarDisplay("thinking");
            m_balloon->showText("認識結果: 「" + event.text + "」");
            break;
            
        case EventType::AIRequestSent:
            updateAvatarDisplay("thinking");
            m_balloon->showText("AIの返答を待っています...");
            break;
            
        case EventType::AIResponseReceived:
            updateAvatarDisplay("speaking");
            m_balloon->showText(event.text);
            break;
            
        case EventType::ErrorOccurred:
            updateAvatarDisplay("idle");
            m_balloon->showText("エラーが発生しました: " + event.text);
            break;

        default:
            break;
    }
}
