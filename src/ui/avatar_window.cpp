#include "avatar_window.h"
#include <QFile>
#include <QGroupBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QMouseEvent>
#include <QMenu>
#include <QInputDialog>
#include <QGuiApplication>
#include <QCoreApplication>
#include <QScreen>
#include <QQueue>
#include <QDir>
#include <QDebug>
#include <QFileDialog>
#include <QRandomGenerator>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTabWidget>
#include <QComboBox>
#include <QFormLayout>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

AvatarWindow::AvatarWindow(QWidget *parent)
    : QMainWindow(parent), m_currentState("idle") 
{
    // WebHook用NetworkManagerの初期化 (ヌルポインタクラッシュ防止のため最初期に行う)
    m_webhookNetworkManager = new QNetworkAccessManager(this);
    connect(m_webhookNetworkManager, &QNetworkAccessManager::finished, this, &AvatarWindow::onWebHookReplyFinished);

    // 通常ウィンドウの設定（背景透過なし・枠あり）
    setWindowFlags(Qt::WindowStaysOnTopHint);
    
    // ウィンドウサイズを横方向に拡張 (幅750, 高さ480)
    setFixedSize(750, 480);

    // 中央ウィジェットの作成とメインレイアウト
    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0); // ウィンドウ全体に広げるためマージンを0に
    mainLayout->setSpacing(0);

    // QTabWidgetを中央に配置（ウィンドウ全体にかける）
    m_tabWidget = new QTabWidget(centralWidget);
    m_tabWidget->setTabPosition(QTabWidget::South); // タブバーを下側に配置
    m_chatTab = new QWidget(m_tabWidget);
    m_settingsTab = new QWidget(m_tabWidget);

    // ----------------------------------------------------
    // チャットタブのレイアウト（従来の左右2ペイン）
    // ----------------------------------------------------
    QHBoxLayout *chatMainLayout = new QHBoxLayout(m_chatTab);
    chatMainLayout->setContentsMargins(10, 10, 10, 10);
    chatMainLayout->setSpacing(10);

    // チャットタブの左側ペイン（アバター表示 + 下部チャット入力・操作ボタン）
    QWidget *chatLeftPanel = new QWidget(m_chatTab);
    QVBoxLayout *chatLeftLayout = new QVBoxLayout(chatLeftPanel);
    chatLeftLayout->setContentsMargins(0, 0, 0, 0);
    chatLeftLayout->setSpacing(10);

    // アバター表示エリア（上部）
    m_avatarLabel = new QLabel(chatLeftPanel);
    m_avatarLabel->setAlignment(Qt::AlignCenter);
    m_avatarLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    chatLeftLayout->addWidget(m_avatarLabel);

    // 入力コントロールエリア（下部）
    QHBoxLayout *controlLayout = new QHBoxLayout();
    controlLayout->setSpacing(5);

    m_inputEdit = new QLineEdit(chatLeftPanel);
    m_inputEdit->setPlaceholderText("メッセージを入力...");
    connect(m_inputEdit, &QLineEdit::returnPressed, this, &AvatarWindow::onSendClicked);

    m_sendButton = new QPushButton("送信", chatLeftPanel);
    connect(m_sendButton, &QPushButton::clicked, this, &AvatarWindow::onSendClicked);

    m_sttButton = new QPushButton("音声", chatLeftPanel);
    connect(m_sttButton, &QPushButton::clicked, this, &AvatarWindow::onSttClicked);

    m_menuButton = new QPushButton("⚙", chatLeftPanel);
    m_menuButton->setFixedWidth(30);
    connect(m_menuButton, &QPushButton::clicked, this, &AvatarWindow::onMenuClicked);

    controlLayout->addWidget(m_inputEdit);
    controlLayout->addWidget(m_sendButton);
    controlLayout->addWidget(m_sttButton);
    controlLayout->addWidget(m_menuButton);

    chatLeftLayout->addLayout(controlLayout);
    chatMainLayout->addWidget(chatLeftPanel);

    // チャットタブの右側ペイン（最新AI応答表示 - 吹き出し風装飾）
    m_rightPanel = new QWidget(m_chatTab);
    m_rightPanel->setObjectName("rightPanel");
    m_rightPanel->setStyleSheet(
        "QWidget#rightPanel {"
        "  background-color: rgba(255, 255, 255, 220);"
        "  border: 2px solid rgba(200, 200, 200, 180);"
        "  border-radius: 15px;"
        "}"
    );

    QVBoxLayout *rightLayout = new QVBoxLayout(m_rightPanel);
    rightLayout->setContentsMargins(15, 15, 15, 15);
    rightLayout->setSpacing(0);

    m_responseBrowser = new QTextBrowser(m_rightPanel);
    m_responseBrowser->setStyleSheet(
        "QTextBrowser {"
        "  background-color: transparent;"
        "  border: none;"
        "  font-size: 11pt;"
        "  color: #333333;"
        "}"
    );
    m_responseBrowser->setPlaceholderText("ここにAIからの回答が表示されます。");

    rightLayout->addWidget(m_responseBrowser);
    chatMainLayout->addWidget(m_rightPanel);

    m_tabWidget->addTab(m_chatTab, "チャット");

    // ----------------------------------------------------
    // 設定タブのレイアウト（フォーム）
    // ----------------------------------------------------
    initSettingsTab(m_settingsTab);
    m_tabWidget->addTab(m_settingsTab, "設定");

    mainLayout->addWidget(m_tabWidget);
    setCentralWidget(centralWidget);

    // ステータスバーの初期化
    statusBar()->showMessage("起動しました。待機中...");

    // デフォルト目標位置（画面下部中央）の設定
    QScreen *primaryScreen = QGuiApplication::primaryScreen();
    QRect screenGeometry = primaryScreen->geometry();
    m_desktopTargetPos = QPoint(screenGeometry.width() / 2, screenGeometry.height() / 2);

    // 初回右クリック/メニュー表示のもたつきを解消するためのダミープリロード
    {
        QMenu dummyMenu(this);
        dummyMenu.addAction("dummy");
        dummyMenu.ensurePolished();
    }

    loadSettings();
    processAndCacheImages();
    updateAvatarDisplay("idle");

    // バリアントフレーム内切り替え用タイマーの準備
    m_variantTimer = new QTimer(this);
    connect(m_variantTimer, &QTimer::timeout, this, &AvatarWindow::switchToNextVariant);

    if (m_schedulerEnabled && !m_schedulerEntries.isEmpty()) {
        // パターンスケジューラーモード
        m_schedulerTimer = new QTimer(this);
        m_schedulerTimer->setSingleShot(true);
        connect(m_schedulerTimer, &QTimer::timeout, this, &AvatarWindow::pickNextPattern);
        pickNextPattern(); // 起動後即座に最初のパターンを選択
    } else {
        // スケジューラーなしの場合は従来通り front_variants を起動
        QString firstGroup;
        if (m_allVariantGroups.contains("front_variants")) {
            firstGroup = "front_variants";
        } else if (!m_allVariantGroups.isEmpty()) {
            firstGroup = m_allVariantGroups.firstKey();
        }
        if (!firstGroup.isEmpty()) {
            switchVariantGroup(firstGroup);
        }
    }

    // OBS配信用WebSocketサーバーの開始
    startWebSocketServer();
}

AvatarWindow::~AvatarWindow() {
    stopWebSocketServer();
}

void AvatarWindow::loadSettings() {
    // picディレクトリのパスを実行後ディレクトリ基準で解決（フォールバック付き）
    QString picDir = "pic";

    // PROJECT_SOURCE_DIR マクロによるプロジェクトルートを最優先で確認
#ifdef PROJECT_SOURCE_DIR
    {
        QString candidate = QString(PROJECT_SOURCE_DIR) + "/pic";
        if (QDir(candidate).exists()) {
            picDir = candidate;
        }
    }
#endif
    // 次に実行ファイルの隣に pic/ があるか確認
    if (!QDir(picDir).exists()) {
        QString candidate = QCoreApplication::applicationDirPath() + "/pic";
        if (QDir(candidate).exists()) picDir = candidate;
    }
    if (!QDir(picDir).exists()) {
        QString candidate = QCoreApplication::applicationDirPath() + "/../pic";
        if (QDir(candidate).exists()) picDir = QDir(candidate).canonicalPath();
    }
    if (!QDir(picDir).exists()) {
        QString candidate = QCoreApplication::applicationDirPath() + "/../../pic";
        if (QDir(candidate).exists()) picDir = QDir(candidate).canonicalPath();
    }

    qDebug() << "AvatarWindow: picDir resolved to:" << picDir;
    QDir().mkpath(picDir);

    QString configPath = picDir + "/avatar_settings.json";

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

        defaultSettings["idle"]      = createSetting("idle.png");
        defaultSettings["listening"] = createSetting("listening.png");
        defaultSettings["thinking"]  = createSetting("thinking.png");
        defaultSettings["speaking"]  = createSetting("speaking.png");

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
                    setting.filePath = picDir + "/" + stateObj["file"].toString();
                    setting.anchorX = stateObj["anchorX"].toInt(100);
                    setting.anchorY = stateObj["anchorY"].toInt(100);
                    setting.transparentX = stateObj["transparentX"].toInt(0);
                    setting.transparentY = stateObj["transparentY"].toInt(0);
                    m_imageSettings[state] = setting;
                }
            }

            // *_variants グループを汎用ロード
            for (const QString &key : obj.keys()) {
                if (!key.endsWith("_variants")) continue;
                if (!obj[key].isObject()) continue;

                QJsonObject varObj = obj[key].toObject();
                FrontVariantSettings group;
                group.label        = varObj["label"].toString(key);
                group.anchorX      = varObj["anchorX"].toInt(100);
                group.anchorY      = varObj["anchorY"].toInt(100);
                group.transparentX = varObj["transparentX"].toInt(0);
                group.transparentY = varObj["transparentY"].toInt(0);
                group.intervalMs   = varObj["interval_ms"].toInt(5000);

                if (varObj.contains("files") && varObj["files"].isArray()) {
                    for (const QJsonValue &v : varObj["files"].toArray()) {
                        if (v.isObject()) {
                            QJsonObject entry = v.toObject();
                            FrontVariantEntry e;
                            e.filePath = picDir + "/" + entry["file"].toString();
                            e.weight   = entry["weight"].toInt(1);
                            if (e.weight < 1) e.weight = 1;
                            group.entries.append(e);
                        }
                    }
                }

                if (!group.isEmpty()) {
                    m_allVariantGroups[key] = group;
                    qDebug() << "Loaded variant group:" << key << "entries:" << group.entries.size();
                }
            }

            // animations セクションの読み込み
            if (obj.contains("animations") && obj["animations"].isObject()) {
                QJsonObject animsObj = obj["animations"].toObject();
                for (const QString &animName : animsObj.keys()) {
                    if (!animsObj[animName].isObject()) continue;
                    QJsonObject animObj = animsObj[animName].toObject();

                    AnimationSequence seq;
                    seq.label           = animObj["label"].toString(animName);
                    seq.anchorX         = animObj["anchorX"].toInt(100);
                    seq.anchorY         = animObj["anchorY"].toInt(100);
                    seq.transparentX    = animObj["transparentX"].toInt(0);
                    seq.transparentY    = animObj["transparentY"].toInt(0);
                    seq.frameIntervalMs = animObj["frame_interval_ms"].toInt(150);
                    seq.loop            = animObj["loop"].toBool(true);

                    if (animObj.contains("frames") && animObj["frames"].isArray()) {
                        for (const QJsonValue &fv : animObj["frames"].toArray()) {
                            seq.frames.append(picDir + "/" + fv.toString());
                        }
                    }

                    if (!seq.frames.isEmpty()) {
                        m_animations[animName] = seq;
                        qDebug() << "Loaded animation:" << animName << "frames:" << seq.frames.size();
                    }
                }
            }

            // pattern_scheduler の読み込み
            if (obj.contains("pattern_scheduler") && obj["pattern_scheduler"].isObject()) {
                QJsonObject schedObj = obj["pattern_scheduler"].toObject();
                m_schedulerEnabled = schedObj["enabled"].toBool(false);

                if (m_schedulerEnabled && schedObj.contains("entries") && schedObj["entries"].isArray()) {
                    int cumulative = 0;
                    for (const QJsonValue &ev : schedObj["entries"].toArray()) {
                        if (!ev.isObject()) continue;
                        QJsonObject e = ev.toObject();
                        PatternSchedulerEntry entry;
                        entry.type   = e["type"].toString();
                        entry.name   = e["name"].toString();
                        entry.weight = e["weight"].toInt(1);
                        entry.stayMs = e["stay_ms"].toInt(8000);
                        if (entry.weight < 1) entry.weight = 1;
                        m_schedulerEntries.append(entry);
                        cumulative += entry.weight;
                        m_schedulerWeights.append(cumulative);
                        qDebug() << "Scheduler entry:" << entry.type << entry.name
                                 << "weight:" << entry.weight << "stay_ms:" << entry.stayMs;
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
        QImage dummy(200, 200, QImage::Format_ARGB32);
        dummy.fill(Qt::transparent);
        return QPixmap::fromImage(dummy);
    }

    image = image.convertToFormat(QImage::Format_ARGB32);
    int width  = image.width();
    int height = image.height();

    if (tx < 0 || tx >= width || ty < 0 || ty >= height) {
        tx = 0; ty = 0;
    }

    // 4隅のシード候補から「背景色らしい」色（最もグリーン成分が高い）を基準色とする
    // これにより(0,0)がキャラクターの一部の場合も他の隅から正しい背景色を取得できる
    QList<QPoint> corners = {
        QPoint(tx, ty),
        QPoint(0, 0),
        QPoint(width - 1, 0),
        QPoint(0, height - 1),
        QPoint(width - 1, height - 1)
    };

    // 最もグリーン成分が高いピクセルを背景色として採用
    QRgb targetColor = image.pixel(tx, ty);
    for (const QPoint &c : corners) {
        QRgb col = image.pixel(c);
        // グリーン > レッド かつ グリーン > ブルー という「緑背景らしさ」で選ぶ
        if (qGreen(col) > qRed(col) && qGreen(col) > qBlue(col)) {
            if (qGreen(col) > qGreen(targetColor)) {
                targetColor = col;
            }
        }
    }

    int tR = qRed(targetColor);
    int tG = qGreen(targetColor);
    int tB = qBlue(targetColor);

    // 色許容範囲（アンチエイリアス端のグリーン混色ピクセルも透過する）
    const int kTolerance = 40;
    auto isSimilar = [&](QRgb c) -> bool {
        return qAbs(qRed(c)   - tR) <= kTolerance &&
               qAbs(qGreen(c) - tG) <= kTolerance &&
               qAbs(qBlue(c)  - tB) <= kTolerance;
    };

    // BFSによる Flood Fill（4隅すべてをシードとして開始）
    QQueue<QPoint> queue;
    QVector<QVector<bool>> visited(width, QVector<bool>(height, false));

    auto enqueueIfSeed = [&](QPoint p) {
        if (p.x() < 0 || p.x() >= width || p.y() < 0 || p.y() >= height) return;
        if (!visited[p.x()][p.y()] && isSimilar(image.pixel(p))) {
            visited[p.x()][p.y()] = true;
            queue.enqueue(p);
        }
    };

    // 指定座標 + 4隅すべてからシード
    enqueueIfSeed(QPoint(tx, ty));
    enqueueIfSeed(QPoint(0, 0));
    enqueueIfSeed(QPoint(width - 1, 0));
    enqueueIfSeed(QPoint(0, height - 1));
    enqueueIfSeed(QPoint(width - 1, height - 1));

    const int dx[] = {0, 0, 1, -1};
    const int dy[] = {1, -1, 0, 0};

    while (!queue.isEmpty()) {
        QPoint p = queue.dequeue();
        image.setPixel(p, 0x00000000);

        for (int i = 0; i < 4; ++i) {
            int nx = p.x() + dx[i];
            int ny = p.y() + dy[i];
            if (nx >= 0 && nx < width && ny >= 0 && ny < height && !visited[nx][ny]) {
                visited[nx][ny] = true;
                if (isSimilar(image.pixel(QPoint(nx, ny)))) {
                    queue.enqueue(QPoint(nx, ny));
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

    // バリアントグループの全キャッシュ構範
    m_allVariantCaches.clear();
    m_allVariantWeights.clear();
    for (auto it = m_allVariantGroups.begin(); it != m_allVariantGroups.end(); ++it) {
        const QString &groupName = it.key();
        const FrontVariantSettings &grp = it.value();
        QVector<QPixmap> cache;
        QVector<int> weights;
        int cumulative = 0;
        for (const FrontVariantEntry &entry : grp.entries) {
            if (QFile::exists(entry.filePath)) {
                cache.append(applyTransparency(entry.filePath, grp.transparentX, grp.transparentY));
                cumulative += entry.weight;
                weights.append(cumulative);
                qDebug() << "Cached variant [" << groupName << "]:" << entry.filePath;
            } else {
                qWarning() << "Variant image not found:" << entry.filePath;
            }
        }
        m_allVariantCaches[groupName] = cache;
        m_allVariantWeights[groupName] = weights;
    }

    // アニメーションフレームの全キャッシュ
    m_animPixmapCache.clear();
    for (auto it = m_animations.begin(); it != m_animations.end(); ++it) {
        const QString &animName = it.key();
        const AnimationSequence &seq = it.value();
        QVector<QPixmap> frames;
        for (const QString &fp : seq.frames) {
            if (QFile::exists(fp)) {
                frames.append(applyTransparency(fp, seq.transparentX, seq.transparentY));
            } else {
                qWarning() << "Animation frame not found:" << fp;
                QImage dummy(200, 200, QImage::Format_ARGB32);
                dummy.fill(Qt::transparent);
                frames.append(QPixmap::fromImage(dummy));
            }
        }
        m_animPixmapCache[animName] = frames;
        qDebug() << "Cached animation:" << animName << "(" << frames.size() << "frames)";
    }
}

void AvatarWindow::updateAvatarDisplay(const QString &state) {
    if (!m_pixmapCache.contains(state)) return;
    m_currentState = state;
    m_isFrontVariantMode = false; // 状態切り替え時はバリアントモードを一時停止
    updateWindowPosition();
    notifyAvatarChanged();
}

void AvatarWindow::switchToNextVariant() {
    if (!m_allVariantCaches.contains(m_activeVariantGroupName)) return;
    const QVector<QPixmap> &cache   = m_allVariantCaches[m_activeVariantGroupName];
    const QVector<int>     &weights = m_allVariantWeights[m_activeVariantGroupName];
    const FrontVariantSettings &grp = m_allVariantGroups[m_activeVariantGroupName];
    if (cache.isEmpty() || weights.isEmpty()) return;

    int totalWeight = weights.last();
    int count       = cache.size();
    int nextIndex   = m_currentFrontIndex;

    int maxTry = count * 10;
    for (int i = 0; i < maxTry; ++i) {
        int rnd = static_cast<int>(QRandomGenerator::global()->bounded(totalWeight));
        int idx = 0;
        for (int j = 0; j < weights.size(); ++j) {
            if (rnd < weights[j]) { idx = j; break; }
        }
        if (count <= 1 || idx != m_currentFrontIndex) {
            nextIndex = idx;
            break;
        }
    }
    m_currentFrontIndex = nextIndex;
    m_isFrontVariantMode = true;

    QPixmap px = cache[m_currentFrontIndex];
    m_avatarLabel->setFixedSize(px.size());
    m_avatarLabel->setPixmap(px);
    adjustSize();
    notifyAvatarChanged();

    // アニメーション切り替え時はウィンドウ位置を保持（ドラッグで移動させた位置は保存される）
    // ウィンドウサイズが変わった場合はレイアウトが自動調整される
    // バルーンはウィンドウ上部固定位置に表示（子ウィジェット座標系）
}

void AvatarWindow::switchVariantGroup(const QString &groupName) {
    if (!m_allVariantGroups.contains(groupName)) {
        qWarning() << "switchVariantGroup: group not found:" << groupName;
        return;
    }
    if (m_animTimer) m_animTimer->stop();
    m_currentAnimation.clear();

    m_activeVariantGroupName = groupName;
    m_currentFrontIndex = 0;
    m_isFrontVariantMode = true;

    // スケジューラーモードの場合は内部ローテーションタイマーを起動しない（スケジューラーが次を決める）
    if (!m_schedulerEnabled) {
        int intervalMs = m_allVariantGroups[groupName].intervalMs;
        if (m_variantTimer) m_variantTimer->start(intervalMs);
    } else {
        if (m_variantTimer) m_variantTimer->stop();
    }

    switchToNextVariant(); // 1枚を即座に表示
    qDebug() << "Switched to variant group:" << groupName;
}

void AvatarWindow::pickNextPattern() {
    // スケジューラー一時停止中は何もしない
    if (m_schedulerPaused) return;
    if (m_schedulerEntries.isEmpty() || m_schedulerWeights.isEmpty()) return;

    // 累積重みテーブルで重み付きランダム選択（連続同一回避）
    int total = m_schedulerWeights.last();
    int count = m_schedulerEntries.size();
    int selectedIdx = 0;

    int maxTry = count * 10;
    for (int i = 0; i < maxTry; ++i) {
        int rnd = static_cast<int>(QRandomGenerator::global()->bounded(total));
        for (int j = 0; j < m_schedulerWeights.size(); ++j) {
            if (rnd < m_schedulerWeights[j]) { selectedIdx = j; break; }
        }
        if (count <= 1 || m_schedulerEntries[selectedIdx].name != m_lastScheduledName) break;
    }

    const PatternSchedulerEntry &entry = m_schedulerEntries[selectedIdx];
    m_lastScheduledName = entry.name;
    qDebug() << "Scheduler: picked" << entry.type << entry.name;

    if (entry.type == "variant_group") {
        switchVariantGroup(entry.name);
        if (m_schedulerTimer) m_schedulerTimer->start(entry.stayMs);
    } else if (entry.type == "animation") {
        playAnimation(entry.name, true);
    }
}

void AvatarWindow::pauseScheduler() {
    if (!m_schedulerEnabled) return;
    m_schedulerPaused = true;
    if (m_schedulerTimer) m_schedulerTimer->stop();
    if (m_variantTimer)   m_variantTimer->stop();
    if (m_animTimer)      m_animTimer->stop();
    if (m_resumeTimer)    m_resumeTimer->stop();
    qDebug() << "Scheduler: paused for AI interaction";
}

void AvatarWindow::resumeScheduler() {
    if (!m_schedulerEnabled) return;
    m_schedulerPaused = false;
    m_animAutoPlay = false;
    qDebug() << "Scheduler: resumed";
    // 少し間を置いてから次のパターンへ
    QTimer::singleShot(500, this, &AvatarWindow::pickNextPattern);
}

// -------------------------------------------------------
// シーケンシャルアニメーション
// -------------------------------------------------------
void AvatarWindow::playAnimation(const QString &name, bool autoPlay) {
    if (!m_animPixmapCache.contains(name) || m_animPixmapCache[name].isEmpty()) {
        qWarning() << "playAnimation: animation not found or empty:" << name;
        // スケジューラーモードなら次に進む
        if (autoPlay && m_schedulerEnabled) pickNextPattern();
        return;
    }

    if (m_variantTimer) m_variantTimer->stop();
    if (m_animTimer)    m_animTimer->stop();
    if (m_schedulerTimer) m_schedulerTimer->stop();

    m_currentAnimation = name;
    m_animFrameIndex   = 0;
    m_animAutoPlay     = autoPlay;

    stepAnimationFrame();

    int interval = m_animations[name].frameIntervalMs;
    if (!m_animTimer) {
        m_animTimer = new QTimer(this);
        connect(m_animTimer, &QTimer::timeout, this, &AvatarWindow::stepAnimationFrame);
    }
    m_animTimer->start(interval);
}

void AvatarWindow::stepAnimationFrame() {
    if (m_currentAnimation.isEmpty() || !m_animPixmapCache.contains(m_currentAnimation)) return;

    const QVector<QPixmap> &frames = m_animPixmapCache[m_currentAnimation];
    const AnimationSequence &seq   = m_animations[m_currentAnimation];
    if (frames.isEmpty()) return;

    // 現在フレームを表示
    QPixmap px = frames[m_animFrameIndex];
    m_avatarLabel->setFixedSize(px.size());
    m_avatarLabel->setPixmap(px);
    adjustSize();
    notifyAvatarChanged();

    // アニメーション再生中はウィンドウ位置を保持（既に設定済みの位置を保存）
    // ウィンドウサイズが変わった場合のみレイアウト調整
    // バルーンはウィンドウ上部中央固定位置に表示

    // 次のフレームへ進む
    m_animFrameIndex++;
    if (m_animFrameIndex >= frames.size()) {
        if (seq.loop && !m_animAutoPlay) {
            m_animFrameIndex = 0;  // 手動選択時はループ
        } else {
            // 再生完了
            if (m_animTimer) m_animTimer->stop();
            m_currentAnimation.clear();
            if (m_animAutoPlay && m_schedulerEnabled) {
                // スケジューラー自動模 → 次のパターンへ
                QTimer::singleShot(300, this, &AvatarWindow::pickNextPattern);
            } else {
                // 手動選択時 → 現在のバリアントグループに戻る
                if (!m_activeVariantGroupName.isEmpty()) {
                    switchVariantGroup(m_activeVariantGroupName);
                }
            }
        }
    }
}

void AvatarWindow::updateWindowPosition() {
    if (!m_pixmapCache.contains(m_currentState) || !m_imageSettings.contains(m_currentState)) return;
    
    QPixmap currentPixmap = m_pixmapCache[m_currentState];
    ImageSetting setting = m_imageSettings[m_currentState];

    m_avatarLabel->setFixedSize(currentPixmap.size());
    m_avatarLabel->setPixmap(currentPixmap);
    // adjustSize() は呼ばない（ウィンドウサイズを固定に保つ）

    // ユーザーがドラッグで移動した、または初期配置済みの場合はその位置を保持
    if (!m_lastWindowPos.isNull()) {
        this->move(m_lastWindowPos);
    } else {
        // ドラッグされておらず、まだ初期配置されていない場合のみ計算位置に配置
        int newX = m_desktopTargetPos.x() - setting.anchorX;
        int newY = m_desktopTargetPos.y() - setting.anchorY;
        this->move(newX, newY);
        m_lastWindowPos = QPoint(newX, newY);
    }

}

void AvatarWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        m_userDraggedWindow = true;  // ドラッグ開始フラグを立てる
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
        
        // ドラッグ中は常に現在位置を保存（状態切り替え時に位置復元用）
        m_lastWindowPos = pos();
        
        event->accept();
    }
}

void AvatarWindow::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && m_userDraggedWindow) {
        // ドラッグ完了時の最後のウィンドウ位置を保存
        m_lastWindowPos = pos();
        event->accept();
    }
}

void AvatarWindow::contextMenuEvent(QContextMenuEvent *event) {
    showContextMenu(event->globalPos());
}

void AvatarWindow::showContextMenu(const QPoint &globalPos) {
    QMenu menu(this);
    
    QAction *actResetHistory = menu.addAction("会話履歴をクリアして要約");
    QAction *actImportHistory = menu.addAction("会話履歴をインポート...");
    QAction *actExportHistory = menu.addAction("会話履歴をエクスポート...");

    menu.addSeparator();
    QAction *actQuit = menu.addAction("終了");

    QAction *selected = menu.exec(globalPos);
    if (!selected) return;

    if (selected == actResetHistory) {
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

void AvatarWindow::onSendClicked() {
    if (!m_inputEdit) return;
    QString text = m_inputEdit->text().trimmed();
    if (!text.isEmpty()) {
        enqueueRequest(text);
        m_inputEdit->clear();
    }
}

void AvatarWindow::onSttClicked() {
    emit startSTTRequested();
}

void AvatarWindow::onMenuClicked() {
    if (!m_menuButton) return;
    QPoint pos = m_menuButton->mapToGlobal(QPoint(0, m_menuButton->height()));
    showContextMenu(pos);
}

void AvatarWindow::on_notify_events(const AppEvent &event) {
    qDebug() << "AvatarWindow received event. Type:" << static_cast<int>(event.type) << "Text:" << event.text;

    switch (event.type) {
        case EventType::VoiceInputStarted:
            pauseScheduler();
            updateAvatarDisplay("listening");
            statusBar()->showMessage("音声入力中... 話しかけてください");
            if (m_responseBrowser) {
                m_responseBrowser->setMarkdown("*マイクの音声を聞いています...*");
            }
            break;

        case EventType::VoiceInputCompleted:
            statusBar()->showMessage("音声認識完了: キューに追加されました");
            enqueueRequest(event.text);
            break;

        case EventType::TwitchCommentReceived:
            statusBar()->showMessage("Twitchコメント受信: キューに追加されました");
            enqueueRequest(event.text);
            break;

        case EventType::AIRequestSent:
            // UI駆動で処理するためここでは状態変更のみマイルドに行う
            statusBar()->showMessage("AIの返答を待っています...");
            break;

        case EventType::AIResponseReceived: {
            m_lastResponseText = event.text;
            updateAvatarDisplay("speaking");
            statusBar()->showMessage("AIが応答中");
            if (m_responseBrowser) {
                m_responseBrowser->setMarkdown(event.text);
            }

            // OBSへの通知
            QJsonObject resObj;
            resObj["type"] = "AIResponseReceived";
            resObj["responseText"] = event.text;
            broadcastToOBS(resObj);

            // WebHookへの通知
            if (m_webhookEnabled && !m_webhookUrl.isEmpty()) {
                QJsonObject whObj;
                whObj["event"] = "ai_response";
                whObj["text"] = event.text;
                sendWebHookNotification(whObj);
            }

            // 次の要求が溜まっているか（キューが空でないか）で表示秒数を選択
            int displaySec = m_bubbleDisplayLongSec;
            if (!m_aiRequestQueue.isEmpty()) {
                displaySec = m_bubbleDisplayShortSec;
                qDebug() << "AvatarWindow: Next request is pending. Short duration used:" << displaySec << "s";
            } else {
                qDebug() << "AvatarWindow: No next request. Long duration used:" << displaySec << "s";
            }
            int displayMs = displaySec * 1000;

            if (!m_resumeTimer) {
                m_resumeTimer = new QTimer(this);
                m_resumeTimer->setSingleShot(true);
                connect(m_resumeTimer, &QTimer::timeout, this, [this]() {
                    m_isProcessingAI = false;
                    resumeScheduler();
                    statusBar()->showMessage("待機中...");

                    // 吹き出しを消すための通知（空の文字列を送信）
                    QJsonObject clearObj;
                    clearObj["type"] = "AIResponseReceived";
                    clearObj["responseText"] = "";
                    broadcastToOBS(clearObj);

                    // 次のキューがあれば処理を開始
                    processNextRequest();
                });
            }
            m_resumeTimer->start(displayMs);
            break;
        }

        case EventType::ErrorOccurred:
            updateAvatarDisplay("idle");
            statusBar()->showMessage("エラーが発生しました: " + event.text);
            if (m_responseBrowser) {
                m_responseBrowser->setMarkdown(QString("**エラーが発生しました**:\n%1").arg(event.text));
            }
            // エラー時も一定時間後にスケジューラー再開
            if (!m_resumeTimer) {
                m_resumeTimer = new QTimer(this);
                m_resumeTimer->setSingleShot(true);
                connect(m_resumeTimer, &QTimer::timeout, this, [this]() {
                    resumeScheduler();
                    statusBar()->showMessage("待機中...");
                });
            }
            m_resumeTimer->start(5000);
            break;

        case EventType::SettingsUpdated:
            if (event.extraData.contains("twitch_oauth_token")) {
                m_twitchOAuthToken = event.extraData.value("twitch_oauth_token").toString();
            }
            if (event.extraData.contains("twitch_channel")) {
                QString channel = event.extraData.value("twitch_channel").toString();
                if (!channel.isEmpty()) {
                    m_twitchChannelEdit->setText(channel);
                }
            }
            saveSettingsFromUI();
            loadSettingsToUI();
            statusBar()->showMessage("Twitch OAuth設定が更新され、保存・適用されました。");
            break;

        default:
            break;
    }
}

void AvatarWindow::initSettingsTab(QWidget *parent) {
    // メインの縦レイアウト
    QVBoxLayout *mainLayout = new QVBoxLayout(parent);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(8);

    m_wsPortEdit = new QLineEdit(parent);
    m_twitchChannelEdit = new QLineEdit(parent);
    
    m_twitchClientIdEdit = new QLineEdit(parent);
    m_twitchClientIdEdit->setEchoMode(QLineEdit::Password);

    m_twitchPortEdit = new QLineEdit(parent);
    m_twitchWakeWordEdit = new QLineEdit(parent);
    
    m_twitchWakeWordModeCombo = new QComboBox(parent);
    m_twitchWakeWordModeCombo->addItems({"contains", "prefix"});
    
    m_aiProviderCombo = new QComboBox(parent);
    m_aiProviderCombo->addItems({"mistral", "dummy"});
    
    m_aiApiKeyEdit = new QLineEdit(parent);
    m_aiApiKeyEdit->setEchoMode(QLineEdit::Password);

    m_tavilyApiKeyEdit = new QLineEdit(parent);
    m_tavilyApiKeyEdit->setEchoMode(QLineEdit::Password);

    m_webhookUrlEdit = new QLineEdit(parent);
    m_webhookEnabledCheckbox = new QCheckBox("有効にする", parent);
    m_bubbleShortEdit = new QLineEdit(parent);
    m_bubbleLongEdit = new QLineEdit(parent);

    // 1. OBS / 描画設定グループ
    QGroupBox *obsGroup = new QGroupBox("OBS / 描画設定", parent);
    QFormLayout *obsLayout = new QFormLayout(obsGroup);
    obsLayout->setContentsMargins(10, 10, 10, 10);
    obsLayout->setSpacing(6);
    obsLayout->addRow("WebSocket ポート (OBS用):", m_wsPortEdit);

    // 表示秒数の横並びレイアウト
    QWidget *bubbleDurationWidget = new QWidget(parent);
    QHBoxLayout *bubbleLayout = new QHBoxLayout(bubbleDurationWidget);
    bubbleLayout->setContentsMargins(0, 0, 0, 0);
    bubbleLayout->setSpacing(8);
    
    QLabel *lblShort = new QLabel("次の要求がある時:", parent);
    m_bubbleShortEdit->setFixedWidth(50);
    QLabel *lblShortSec = new QLabel("秒", parent);
    
    QLabel *lblLong = new QLabel("次の要求が無い時:", parent);
    m_bubbleLongEdit->setFixedWidth(50);
    QLabel *lblLongSec = new QLabel("秒", parent);

    bubbleLayout->addWidget(lblShort);
    bubbleLayout->addWidget(m_bubbleShortEdit);
    bubbleLayout->addWidget(lblShortSec);
    bubbleLayout->addSpacing(15);
    bubbleLayout->addWidget(lblLong);
    bubbleLayout->addWidget(m_bubbleLongEdit);
    bubbleLayout->addWidget(lblLongSec);
    bubbleLayout->addStretch();
    obsLayout->addRow("吹き出し表示秒数:", bubbleDurationWidget);
    mainLayout->addWidget(obsGroup);

    // 2. Twitch 連携設定グループ
    QGroupBox *twitchGroup = new QGroupBox("Twitch 連携設定", parent);
    QFormLayout *twitchLayout = new QFormLayout(twitchGroup);
    twitchLayout->setContentsMargins(10, 10, 10, 10);
    twitchLayout->setSpacing(6);
    twitchLayout->addRow("チャンネル:", m_twitchChannelEdit);
    twitchLayout->addRow("クライアント ID:", m_twitchClientIdEdit);
    twitchLayout->addRow("OAuth用ポート:", m_twitchPortEdit);
    
    QWidget *wakeWordWidget = new QWidget(parent);
    QHBoxLayout *wakeWordLayout = new QHBoxLayout(wakeWordWidget);
    wakeWordLayout->setContentsMargins(0, 0, 0, 0);
    wakeWordLayout->setSpacing(8);
    m_twitchWakeWordEdit->setFixedWidth(100);
    wakeWordLayout->addWidget(m_twitchWakeWordEdit);
    wakeWordLayout->addWidget(new QLabel("判定:", parent));
    wakeWordLayout->addWidget(m_twitchWakeWordModeCombo);
    wakeWordLayout->addStretch();
    twitchLayout->addRow("ウェイクワード:", wakeWordWidget);
    mainLayout->addWidget(twitchGroup);

    // 3. AI 設定グループ
    QGroupBox *aiGroup = new QGroupBox("AI 設定", parent);
    QFormLayout *aiLayout = new QFormLayout(aiGroup);
    aiLayout->setContentsMargins(10, 10, 10, 10);
    aiLayout->setSpacing(6);
    aiLayout->addRow("プロバイダ:", m_aiProviderCombo);
    aiLayout->addRow("API キー:", m_aiApiKeyEdit);
    aiLayout->addRow("Tavily キー (任意):", m_tavilyApiKeyEdit);
    mainLayout->addWidget(aiGroup);

    // 4. 外部通知設定グループ (WebHook)
    QGroupBox *notifyGroup = new QGroupBox("外部通知設定 (WebHook)", parent);
    QHBoxLayout *notifyLayout = new QHBoxLayout(notifyGroup);
    notifyLayout->setContentsMargins(10, 10, 10, 10);
    notifyLayout->setSpacing(10);
    
    QLabel *lblWebhookUrl = new QLabel("通知先 URL:", parent);
    m_webhookUrlEdit->setPlaceholderText("http://localhost:4081");
    m_webhookUrlEdit->setMaximumWidth(280); // 半分程度の幅に制限
    
    notifyLayout->addWidget(lblWebhookUrl);
    notifyLayout->addWidget(m_webhookUrlEdit);
    notifyLayout->addWidget(m_webhookEnabledCheckbox);
    notifyLayout->addStretch();
    mainLayout->addWidget(notifyGroup);

    // 保存・適用ボタン
    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *btnSave = new QPushButton("設定を保存して適用", parent);
    btnSave->setFixedHeight(32);
    connect(btnSave, &QPushButton::clicked, this, &AvatarWindow::onSaveSettingsClicked);
    
    QPushButton *btnReauth = new QPushButton("Twitch認証開始", parent);
    btnReauth->setFixedHeight(32);
    connect(btnReauth, &QPushButton::clicked, this, &AvatarWindow::onTwitchReauthClicked);

    btnLayout->addWidget(btnSave);
    btnLayout->addWidget(btnReauth);
    mainLayout->addLayout(btnLayout);

    loadSettingsToUI();
}

void AvatarWindow::loadSettingsToUI() {
    QString configPath = "local_settings.json";
#ifdef PROJECT_SOURCE_DIR
    if (!QFile::exists(configPath)) {
        configPath = QString(PROJECT_SOURCE_DIR) + "/local_settings.json";
    }
#endif
    if (!QFile::exists(configPath)) {
        configPath = QCoreApplication::applicationDirPath() + "/local_settings.json";
    }
    if (!QFile::exists(configPath)) {
        configPath = QCoreApplication::applicationDirPath() + "/../local_settings.json";
    }
    if (!QFile::exists(configPath)) {
        configPath = QCoreApplication::applicationDirPath() + "/../../local_settings.json";
    }

    if (!QFile::exists(configPath)) return;

    QFile file(configPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QByteArray data = file.readAll();
        file.close();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject obj = doc.object();
            m_wsPortEdit->setText(QString::number(obj.value("websocket_port").toInt(ConfigDefaults::WEBSOCKET_PORT)));
            m_twitchChannelEdit->setText(obj.value("twitch_channel").toString());
            m_twitchClientIdEdit->setText(obj.value("twitch_client_id").toString());
            m_twitchPortEdit->setText(QString::number(obj.value("twitch_port").toInt(ConfigDefaults::TWITCH_PORT)));
            if (obj.contains("twitch_wakeword")) {
                m_twitchWakeWordEdit->setText(obj.value("twitch_wakeword").toString());
            } else {
                m_twitchWakeWordEdit->setText(ConfigDefaults::WAKE_WORD);
            }
            
            QString mode = obj.value("twitch_wakeword_mode").toString(ConfigDefaults::WAKE_WORD_MODE);
            int modeIdx = m_twitchWakeWordModeCombo->findText(mode);
            if (modeIdx >= 0) m_twitchWakeWordModeCombo->setCurrentIndex(modeIdx);

            QString provider = obj.value("ai_provider").toString(ConfigDefaults::AI_PROVIDER);
            int provIdx = m_aiProviderCombo->findText(provider);
            if (provIdx >= 0) m_aiProviderCombo->setCurrentIndex(provIdx);

            m_aiApiKeyEdit->setText(obj.value("mistral_api_key").toString());
            m_tavilyApiKeyEdit->setText(obj.value("tavily_api_key").toString());
            m_twitchOAuthToken = obj.value("twitch_oauth_token").toString();
            
            m_webhookUrlEdit->setText(obj.value("webhook_url").toString());
            m_webhookUrl = obj.value("webhook_url").toString();
            
            bool whEnabled = obj.value("webhook_enabled").toBool(false);
            m_webhookEnabledCheckbox->setChecked(whEnabled);
            m_webhookEnabled = whEnabled;

            m_bubbleDisplayShortSec = obj.value("bubble_display_short_sec").toInt(5);
            m_bubbleDisplayLongSec = obj.value("bubble_display_long_sec").toInt(10);
            m_bubbleShortEdit->setText(QString::number(m_bubbleDisplayShortSec));
            m_bubbleLongEdit->setText(QString::number(m_bubbleDisplayLongSec));
        }
    }
}

void AvatarWindow::saveSettingsFromUI() {
    QString configPath = "local_settings.json";
#ifdef PROJECT_SOURCE_DIR
    if (!QFile::exists(configPath)) {
        configPath = QString(PROJECT_SOURCE_DIR) + "/local_settings.json";
    }
#endif
    if (!QFile::exists(configPath)) {
        configPath = QCoreApplication::applicationDirPath() + "/local_settings.json";
    }
    if (!QFile::exists(configPath)) {
        configPath = QCoreApplication::applicationDirPath() + "/../local_settings.json";
    }
    if (!QFile::exists(configPath)) {
        configPath = QCoreApplication::applicationDirPath() + "/../../local_settings.json";
    }

    QJsonObject obj;
    QFile file(configPath);
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QByteArray data = file.readAll();
        file.close();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isNull() && doc.isObject()) {
            obj = doc.object();
        }
    }

    m_webhookUrl = m_webhookUrlEdit->text().trimmed();
    m_webhookEnabled = m_webhookEnabledCheckbox->isChecked();

    obj["websocket_port"] = m_wsPortEdit->text().trimmed().toInt();
    obj["twitch_channel"] = m_twitchChannelEdit->text().trimmed();
    obj["twitch_client_id"] = m_twitchClientIdEdit->text().trimmed();
    obj["twitch_port"] = m_twitchPortEdit->text().trimmed().toInt();
    obj["twitch_wakeword"] = m_twitchWakeWordEdit->text().trimmed();
    obj["twitch_wakeword_mode"] = m_twitchWakeWordModeCombo->currentText();
    obj["ai_provider"] = m_aiProviderCombo->currentText();
    obj["mistral_api_key"] = m_aiApiKeyEdit->text().trimmed();
    obj["tavily_api_key"] = m_tavilyApiKeyEdit->text().trimmed();
    obj["twitch_oauth_token"] = m_twitchOAuthToken;
    obj["webhook_url"] = m_webhookUrl;
    obj["webhook_enabled"] = m_webhookEnabled;
    obj["trans_cipher_key"] = obj.value("trans_cipher_key").toString("DefaultCipherKey123");

    m_bubbleDisplayShortSec = m_bubbleShortEdit->text().trimmed().toInt();
    if (m_bubbleDisplayShortSec <= 0) m_bubbleDisplayShortSec = 5;
    m_bubbleDisplayLongSec = m_bubbleLongEdit->text().trimmed().toInt();
    if (m_bubbleDisplayLongSec <= 0) m_bubbleDisplayLongSec = 10;

    obj["bubble_display_short_sec"] = m_bubbleDisplayShortSec;
    obj["bubble_display_long_sec"] = m_bubbleDisplayLongSec;

    QJsonDocument newDoc(obj);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(newDoc.toJson(QJsonDocument::Indented));
        file.close();
        qDebug() << "AvatarWindow: Settings saved to" << configPath;
    }

    // pic/avatar_obs.htmlのWebSocketポート記述を自動更新
    QString htmlPath = "pic/avatar_obs.html";
#ifdef PROJECT_SOURCE_DIR
    if (!QFile::exists(htmlPath)) {
        htmlPath = QString(PROJECT_SOURCE_DIR) + "/pic/avatar_obs.html";
    }
#endif
    if (!QFile::exists(htmlPath)) {
        htmlPath = QCoreApplication::applicationDirPath() + "/pic/avatar_obs.html";
    }
    if (QFile::exists(htmlPath)) {
        QFile htmlFile(htmlPath);
        if (htmlFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString content = htmlFile.readAll();
            htmlFile.close();

            int wsPort = obj["websocket_port"].toInt();
            if (wsPort <= 0) wsPort = ConfigDefaults::WEBSOCKET_PORT;

            QRegularExpression re("ws://localhost:\\d+");
            content.replace(re, QString("ws://localhost:%1").arg(wsPort));

            if (htmlFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                htmlFile.write(content.toUtf8());
                htmlFile.close();
                qDebug() << "AvatarWindow: Updated avatar_obs.html port to" << wsPort;
            }
        }
    }
}

void AvatarWindow::onSaveSettingsClicked() {
    saveSettingsFromUI();
    // WebSocket サーバー再起動
    stopWebSocketServer();
    startWebSocketServer();
    // コアへ設定更新を通知
    emit settingsUpdated();
    statusBar()->showMessage("設定を保存して適用しました。");
}

void AvatarWindow::onTwitchReauthClicked() {
    saveSettingsFromUI();
    emit settingsUpdated();
    emit twitchReauthRequested();
    statusBar()->showMessage("Twitch 認証を開始します...");
}

// OBS WebSocket サーバーの制御
void AvatarWindow::startWebSocketServer() {
    int port = ConfigDefaults::WEBSOCKET_PORT;
    QString configPath = "local_settings.json";
#ifdef PROJECT_SOURCE_DIR
    if (!QFile::exists(configPath)) {
        configPath = QString(PROJECT_SOURCE_DIR) + "/local_settings.json";
    }
#endif
    if (!QFile::exists(configPath)) {
        configPath = QCoreApplication::applicationDirPath() + "/local_settings.json";
    }
    if (!QFile::exists(configPath)) {
        configPath = QCoreApplication::applicationDirPath() + "/../local_settings.json";
    }
    if (!QFile::exists(configPath)) {
        configPath = QCoreApplication::applicationDirPath() + "/../../local_settings.json";
    }

    if (QFile::exists(configPath)) {
        QFile file(configPath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (!doc.isNull() && doc.isObject()) {
                port = doc.object().value("websocket_port").toInt(ConfigDefaults::WEBSOCKET_PORT);
            }
            file.close();
        }
    }

    m_wsServer = new QWebSocketServer("OBS Streamer Agent", QWebSocketServer::NonSecureMode, this);
    if (m_wsServer->listen(QHostAddress::Any, port)) {
        qDebug() << "AvatarWindow: OBS WebSocket server listening on port" << port;
        connect(m_wsServer, &QWebSocketServer::newConnection, this, &AvatarWindow::onNewWSConnection);
    } else {
        qWarning() << "AvatarWindow: Failed to start OBS WebSocket server on port" << port;
    }
}

void AvatarWindow::stopWebSocketServer() {
    if (m_wsServer) {
        m_wsServer->close();
        qDeleteAll(m_wsClients.begin(), m_wsClients.end());
        m_wsClients.clear();
        delete m_wsServer;
        m_wsServer = nullptr;
        qDebug() << "AvatarWindow: OBS WebSocket server stopped.";
    }
}

void AvatarWindow::onNewWSConnection() {
    QWebSocket *client = m_wsServer->nextPendingConnection();
    if (client) {
        qDebug() << "AvatarWindow: OBS WebSocket client connected from" << client->peerAddress().toString();
        connect(client, &QWebSocket::textMessageReceived, this, [client](const QString &msg) {
            qDebug() << "Received from OBS client:" << msg;
        });
        connect(client, &QWebSocket::disconnected, this, &AvatarWindow::onWSClientDisconnected);
        m_wsClients.append(client);

        // 接続直後の状態初期化通知
        QJsonObject initObj;
        initObj["type"] = "Init";
        initObj["state"] = m_currentState;
        initObj["lastResponseText"] = m_lastResponseText;
        
        // 画像名の通知
        QString currentImgPath;
        if (m_isFrontVariantMode && m_allVariantGroups.contains(m_activeVariantGroupName)) {
            const auto &entries = m_allVariantGroups[m_activeVariantGroupName].entries;
            if (m_currentFrontIndex >= 0 && m_currentFrontIndex < entries.size()) {
                currentImgPath = entries[m_currentFrontIndex].filePath;
            }
        } else if (!m_currentAnimation.isEmpty() && m_animations.contains(m_currentAnimation)) {
            const auto &frames = m_animations[m_currentAnimation].frames;
            if (m_animFrameIndex >= 0 && m_animFrameIndex < frames.size()) {
                currentImgPath = frames[m_animFrameIndex];
            }
        } else if (m_imageSettings.contains(m_currentState)) {
            currentImgPath = m_imageSettings[m_currentState].filePath;
        }
        initObj["avatarImage"] = QFileInfo(currentImgPath).fileName();

        client->sendTextMessage(QJsonDocument(initObj).toJson(QJsonDocument::Compact));
    }
}

void AvatarWindow::onWSClientDisconnected() {
    QWebSocket *client = qobject_cast<QWebSocket *>(sender());
    if (client) {
        m_wsClients.removeAll(client);
        client->deleteLater();
        qDebug() << "AvatarWindow: OBS WebSocket client disconnected.";
    }
}

void AvatarWindow::broadcastToOBS(const QJsonObject &json) {
    QByteArray data = QJsonDocument(json).toJson(QJsonDocument::Compact);
    for (QWebSocket *client : m_wsClients) {
        client->sendTextMessage(data);
    }
}

void AvatarWindow::notifyAvatarChanged() {
    // 現在の画像ファイル名を取得
    QString currentImgPath;
    if (m_isFrontVariantMode) {
        if (m_allVariantGroups.contains(m_activeVariantGroupName)) {
            const auto &entries = m_allVariantGroups[m_activeVariantGroupName].entries;
            if (m_currentFrontIndex >= 0 && m_currentFrontIndex < entries.size()) {
                currentImgPath = entries[m_currentFrontIndex].filePath;
            }
        }
    } else if (!m_currentAnimation.isEmpty() && m_animations.contains(m_currentAnimation)) {
        const auto &frames = m_animations[m_currentAnimation].frames;
        if (m_animFrameIndex >= 0 && m_animFrameIndex < frames.size()) {
            currentImgPath = frames[m_animFrameIndex];
        }
    } else if (m_imageSettings.contains(m_currentState)) {
        currentImgPath = m_imageSettings[m_currentState].filePath;
    }

    QString filename = QFileInfo(currentImgPath).fileName();
    
    // 設定されているアンカー座標を取得
    int anchorX = 100;
    int anchorY = 100;
    if (m_isFrontVariantMode && m_allVariantGroups.contains(m_activeVariantGroupName)) {
        anchorX = m_allVariantGroups[m_activeVariantGroupName].anchorX;
        anchorY = m_allVariantGroups[m_activeVariantGroupName].anchorY;
    } else if (!m_currentAnimation.isEmpty() && m_animations.contains(m_currentAnimation)) {
        anchorX = m_animations[m_currentAnimation].anchorX;
        anchorY = m_animations[m_currentAnimation].anchorY;
    } else if (m_imageSettings.contains(m_currentState)) {
        anchorX = m_imageSettings[m_currentState].anchorX;
        anchorY = m_imageSettings[m_currentState].anchorY;
    }

    QJsonObject obj;
    obj["type"] = "AvatarChanged";
    obj["state"] = m_currentState;
    obj["avatarImage"] = filename;
    obj["anchorX"] = anchorX;
    obj["anchorY"] = anchorY;

    broadcastToOBS(obj);

    // WebHookへの通知
    if (m_webhookEnabled && !m_webhookUrl.isEmpty()) {
        QJsonObject whObj;
        whObj["event"] = "avatar_changed";
        whObj["state"] = m_currentState;
        whObj["image"] = filename;
        whObj["anchorX"] = anchorX;
        whObj["anchorY"] = anchorY;
        sendWebHookNotification(whObj);
    }
}

void AvatarWindow::sendWebHookNotification(const QJsonObject &json) {
    if (!m_webhookEnabled || m_webhookUrl.isEmpty()) return;

    QUrl url(m_webhookUrl);
    if (!url.isValid()) {
        qWarning() << "AvatarWindow: WebHook URL is invalid:" << m_webhookUrl;
        return;
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QByteArray data = QJsonDocument(json).toJson(QJsonDocument::Compact);
    m_webhookNetworkManager->post(request, data);
}

void AvatarWindow::onWebHookReplyFinished(QNetworkReply *reply) {
    if (reply) {
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "AvatarWindow: WebHook notification failed:" << reply->errorString();
        } else {
            qDebug() << "AvatarWindow: WebHook notification sent successfully. Code:"
                     << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        }
        reply->deleteLater();
    }
}

void AvatarWindow::enqueueRequest(const QString &text) {
    if (text.trimmed().isEmpty()) return;
    m_aiRequestQueue.enqueue(text.trimmed());
    qDebug() << "AvatarWindow: Enqueued AI request. Current queue size:" << m_aiRequestQueue.size();
    processNextRequest();
}

void AvatarWindow::processNextRequest() {
    if (m_isProcessingAI) {
        qDebug() << "AvatarWindow: Already processing AI. Waiting...";
        return;
    }
    if (m_aiRequestQueue.isEmpty()) {
        qDebug() << "AvatarWindow: Queue is empty.";
        return;
    }

    m_isProcessingAI = true;
    QString nextPrompt = m_aiRequestQueue.dequeue();
    qDebug() << "AvatarWindow: Processing next request:" << nextPrompt;

    pauseScheduler();
    updateAvatarDisplay("thinking");
    statusBar()->showMessage("AIの返答を待っています...");
    if (m_responseBrowser) {
        m_responseBrowser->setMarkdown("*AIの返答を待っています...*");
    }

    // AIクライアントへリクエストを要求するシグナルを発火
    emit requestAIExecution(nextPrompt);
}
