#include "avatar_window.h"
#include <QFile>
#include <QGroupBox>
#include <QScrollArea>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QMenu>
#include <QInputDialog>
#include <QGuiApplication>
#include <QClipboard>
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
#include <QTableWidget>
#include <QHeaderView>
#include <QMessageBox>
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
    // setWindowFlags(Qt::WindowStaysOnTopHint); // 最前面表示をやめるためコメントアウト
    
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

    m_nicknameTab = new QWidget(m_tabWidget);
    initNicknameTab(m_nicknameTab);
    m_tabWidget->addTab(m_nicknameTab, "ニックネーム");

    m_knowledgeTab = new QWidget(m_tabWidget);
    initKnowledgeTab(m_knowledgeTab);
    m_tabWidget->addTab(m_knowledgeTab, "ナレッジ");

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
    if (m_isInitialized && !m_lastWindowPos.isNull()) {
        this->move(m_lastWindowPos);
    } else {
        // ドラッグされておらず、まだ初期配置されていない場合のみ計算位置に配置
        int newX = m_desktopTargetPos.x() - setting.anchorX;
        int newY = m_desktopTargetPos.y() - setting.anchorY;
        this->move(newX, newY);
        m_lastWindowPos = QPoint(newX, newY);
        m_isInitialized = true;
    }

}



void AvatarWindow::moveEvent(QMoveEvent *event) {
    QMainWindow::moveEvent(event);
    if (m_isInitialized) {
        m_lastWindowPos = pos();
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
            {
                QString username = event.extraData.value("user").toString();
                QString twitchChannel = event.extraData.value("twitch_channel").toString();
                QString encodedUser = twitchChannel.isEmpty()
                    ? QString("[Twitch] %1").arg(username)
                    : QString("[Twitch:%1] %2").arg(twitchChannel, username);
                enqueueRequest(event.text, encodedUser);
            }
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
            }
            m_resumeTimer->disconnect();
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
            }
            m_resumeTimer->disconnect();
            connect(m_resumeTimer, &QTimer::timeout, this, [this]() {
                m_isProcessingAI = false;
                resumeScheduler();
                statusBar()->showMessage("待機中...");
                processNextRequest();
            });
            m_resumeTimer->start(5000);
            break;

        case EventType::SettingsUpdated:
            if (event.extraData.contains("twitch_oauth_token")) {
                m_twitchOAuthToken = event.extraData.value("twitch_oauth_token").toString();
            }
            if (event.extraData.contains("twitch_username")) {
                m_twitchUsername = event.extraData.value("twitch_username").toString();
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
    // タブの最親レイアウト (スクロールエリアを中に収める)
    QVBoxLayout *containerLayout = new QVBoxLayout(parent);
    containerLayout->setContentsMargins(0, 0, 0, 0);

    QScrollArea *scrollArea = new QScrollArea(parent);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    QWidget *scrollContent = new QWidget(scrollArea);
    // スクロールコンテンツ内の縦レイアウト
    QVBoxLayout *mainLayout = new QVBoxLayout(scrollContent);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(8);

    m_wsPortEdit = new QLineEdit(scrollContent);
    m_obsHttpEnabledCheckbox = new QCheckBox("簡易HTTPサーバーを有効にする（別PCのOBS接続用）", scrollContent);
    m_obsHttpPortEdit = new QLineEdit(scrollContent);
    m_twitchChannelEdit = new QLineEdit(scrollContent);
    
    m_twitchClientIdEdit = new QLineEdit(scrollContent);
    m_twitchClientIdEdit->setEchoMode(QLineEdit::Password);

    m_twitchPortEdit = new QLineEdit(scrollContent);
    m_twitchWakeWordEdit = new QLineEdit(scrollContent);
    
    m_twitchWakeWordModeCombo = new QComboBox(scrollContent);
    m_twitchWakeWordModeCombo->addItems({"contains", "prefix"});
    
    m_avatarNameEdit = new QLineEdit(scrollContent);
    m_nameReactionCheckbox = new QCheckBox("名前（アバター名）呼ばれて反応する", scrollContent);
    
    m_aiProviderCombo = new QComboBox(scrollContent);
    m_aiProviderCombo->addItems({"mistral", "dummy"});
    
    m_aiApiKeyEdit = new QLineEdit(scrollContent);
    m_aiApiKeyEdit->setEchoMode(QLineEdit::Password);

    m_tavilyApiKeyEdit = new QLineEdit(scrollContent);
    m_tavilyApiKeyEdit->setEchoMode(QLineEdit::Password);

    m_webhookUrlEdit = new QLineEdit(scrollContent);
    m_webhookEnabledCheckbox = new QCheckBox("有効にする", scrollContent);
    m_bubbleShortEdit = new QLineEdit(scrollContent);
    m_bubbleLongEdit = new QLineEdit(scrollContent);

    // 1. OBS / 描画設定グループ
    QGroupBox *obsGroup = new QGroupBox("OBS / 描画設定", scrollContent);
    QFormLayout *obsLayout = new QFormLayout(obsGroup);
    obsLayout->setContentsMargins(10, 10, 10, 10);
    obsLayout->setSpacing(6);
    obsLayout->addRow("WebSocket ポート (OBS用):", m_wsPortEdit);
    obsLayout->addRow("OBS用HTTPサーバー有効化:", m_obsHttpEnabledCheckbox);
    obsLayout->addRow("HTTP配信ポート:", m_obsHttpPortEdit);

    // 表示秒数の横並びレイアウト
    QWidget *bubbleDurationWidget = new QWidget(scrollContent);
    QHBoxLayout *bubbleLayout = new QHBoxLayout(bubbleDurationWidget);
    bubbleLayout->setContentsMargins(0, 0, 0, 0);
    bubbleLayout->setSpacing(8);
    
    QLabel *lblShort = new QLabel("次の要求がある時:", scrollContent);
    m_bubbleShortEdit->setFixedWidth(50);
    QLabel *lblShortSec = new QLabel("秒", scrollContent);
    
    QLabel *lblLong = new QLabel("次の要求が無い時:", scrollContent);
    m_bubbleLongEdit->setFixedWidth(50);
    QLabel *lblLongSec = new QLabel("秒", scrollContent);

    bubbleLayout->addWidget(lblShort);
    bubbleLayout->addWidget(m_bubbleShortEdit);
    bubbleLayout->addWidget(lblShortSec);
    bubbleLayout->addSpacing(15);
    bubbleLayout->addWidget(lblLong);
    bubbleLayout->addWidget(m_bubbleLongEdit);
    bubbleLayout->addWidget(lblLongSec);
    bubbleLayout->addStretch();
    obsLayout->addRow("吹き出し表示秒数:", bubbleDurationWidget);

    // OBS用ファイルの絶対パス表示とコピーボタン
    QWidget *obsPathWidget = new QWidget(scrollContent);
    QHBoxLayout *obsPathLayout = new QHBoxLayout(obsPathWidget);
    obsPathLayout->setContentsMargins(0, 0, 0, 0);
    obsPathLayout->setSpacing(8);

    m_obsPathEdit = new QLineEdit(scrollContent);
    m_obsPathEdit->setReadOnly(true);
    // パスを解決する
    QString htmlPath = "pic/avatar_obs.html";
#ifdef PROJECT_SOURCE_DIR
    if (!QFile::exists(htmlPath)) {
        htmlPath = QString(PROJECT_SOURCE_DIR) + "/pic/avatar_obs.html";
    }
#endif
    if (!QFile::exists(htmlPath)) {
        htmlPath = QCoreApplication::applicationDirPath() + "/pic/avatar_obs.html";
    }
    m_obsPathEdit->setText(QFileInfo(htmlPath).absoluteFilePath());

    QPushButton *btnCopyObsPath = new QPushButton("パスをコピー", scrollContent);
    btnCopyObsPath->setFixedWidth(100);
    connect(btnCopyObsPath, &QPushButton::clicked, this, &AvatarWindow::onCopyObsPathClicked);

    obsPathLayout->addWidget(m_obsPathEdit);
    obsPathLayout->addWidget(btnCopyObsPath);

    obsLayout->addRow("OBS用ファイルパス:", obsPathWidget);

    mainLayout->addWidget(obsGroup);

    // 2. Twitch 連携設定グループ
    QGroupBox *twitchGroup = new QGroupBox("Twitch 連携設定", scrollContent);
    QFormLayout *twitchLayout = new QFormLayout(twitchGroup);
    twitchLayout->setContentsMargins(10, 10, 10, 10);
    twitchLayout->setSpacing(6);
    twitchLayout->addRow("アバター名:", m_avatarNameEdit);
    twitchLayout->addRow("名前反応:", m_nameReactionCheckbox);
    twitchLayout->addRow("チャンネル:", m_twitchChannelEdit);
    twitchLayout->addRow("クライアント ID:", m_twitchClientIdEdit);
    twitchLayout->addRow("OAuth用ポート:", m_twitchPortEdit);
    
    QWidget *wakeWordWidget = new QWidget(scrollContent);
    QHBoxLayout *wakeWordLayout = new QHBoxLayout(wakeWordWidget);
    wakeWordLayout->setContentsMargins(0, 0, 0, 0);
    wakeWordLayout->setSpacing(8);
    m_twitchWakeWordEdit->setFixedWidth(100);
    wakeWordLayout->addWidget(m_twitchWakeWordEdit);
    wakeWordLayout->addWidget(new QLabel("判定:", scrollContent));
    wakeWordLayout->addWidget(m_twitchWakeWordModeCombo);
    wakeWordLayout->addStretch();
    twitchLayout->addRow("ウェイクワード:", wakeWordWidget);
    mainLayout->addWidget(twitchGroup);

    // 3. AI 設定グループ
    QGroupBox *aiGroup = new QGroupBox("AI 設定", scrollContent);
    QFormLayout *aiLayout = new QFormLayout(aiGroup);
    aiLayout->setContentsMargins(10, 10, 10, 10);
    aiLayout->setSpacing(6);
    aiLayout->addRow("プロバイダ:", m_aiProviderCombo);
    aiLayout->addRow("API キー:", m_aiApiKeyEdit);
    aiLayout->addRow("Tavily キー (任意):", m_tavilyApiKeyEdit);
    mainLayout->addWidget(aiGroup);

    // 4. 外部通知設定グループ (WebHook)
    QGroupBox *notifyGroup = new QGroupBox("外部通知設定 (WebHook)", scrollContent);
    QHBoxLayout *notifyLayout = new QHBoxLayout(notifyGroup);
    notifyLayout->setContentsMargins(10, 10, 10, 10);
    notifyLayout->setSpacing(10);
    
    QLabel *lblWebhookUrl = new QLabel("通知先 URL:", scrollContent);
    m_webhookUrlEdit->setPlaceholderText("http://localhost:4081");
    m_webhookUrlEdit->setMaximumWidth(400); // 400幅に広げる
    
    notifyLayout->addWidget(lblWebhookUrl);
    notifyLayout->addWidget(m_webhookUrlEdit);
    notifyLayout->addWidget(m_webhookEnabledCheckbox);
    notifyLayout->addStretch();
    mainLayout->addWidget(notifyGroup);

    // 5. Discord 連携設定グループ
    QGroupBox *discordGroup = new QGroupBox("Discord 連携設定", scrollContent);
    QFormLayout *discordLayout = new QFormLayout(discordGroup);
    discordLayout->setContentsMargins(10, 10, 10, 10);
    discordLayout->setSpacing(6);
    
    m_discordEnabledCheckbox = new QCheckBox("Discordボット連携を有効化", scrollContent);
    m_discordBotTokenEdit = new QLineEdit(scrollContent);
    m_discordBotTokenEdit->setEchoMode(QLineEdit::Password);
    m_discordBotTokenEdit->setPlaceholderText("ボットのトークンを入力...");

    m_discordChannelIdEdit = new QLineEdit(scrollContent);
    m_discordChannelIdEdit->setPlaceholderText("対象のテキストチャンネルIDを入力...");

    discordLayout->addRow("有効化:", m_discordEnabledCheckbox);
    discordLayout->addRow("ボット トークン:", m_discordBotTokenEdit);
    discordLayout->addRow("チャンネル ID:", m_discordChannelIdEdit);
    mainLayout->addWidget(discordGroup);

    // 保存・適用ボタン
    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *btnSave = new QPushButton("設定を保存して適用", scrollContent);
    btnSave->setFixedHeight(32);
    connect(btnSave, &QPushButton::clicked, this, &AvatarWindow::onSaveSettingsClicked);
    
    QPushButton *btnReauth = new QPushButton("Twitch認証開始", scrollContent);
    btnReauth->setFixedHeight(32);
    connect(btnReauth, &QPushButton::clicked, this, &AvatarWindow::onTwitchReauthClicked);

    btnLayout->addWidget(btnSave);
    btnLayout->addWidget(btnReauth);
    mainLayout->addLayout(btnLayout);

    scrollArea->setWidget(scrollContent);
    containerLayout->addWidget(scrollArea);

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
            m_twitchUsername = obj.value("twitch_username").toString();
            
            m_webhookUrlEdit->setText(obj.value("webhook_url").toString());
            m_webhookUrl = obj.value("webhook_url").toString();
            
            bool whEnabled = obj.value("webhook_enabled").toBool(false);
            m_webhookEnabledCheckbox->setChecked(whEnabled);
            m_webhookEnabled = whEnabled;

            m_bubbleDisplayShortSec = obj.value("bubble_display_short_sec").toInt(5);
            m_bubbleDisplayLongSec = obj.value("bubble_display_long_sec").toInt(10);
            m_bubbleShortEdit->setText(QString::number(m_bubbleDisplayShortSec));
            m_bubbleLongEdit->setText(QString::number(m_bubbleDisplayLongSec));

            m_avatarName = obj.value("avatar_name").toString("AIアシスタント").trimmed();
            m_nameReactionEnabled = obj.value("name_reaction_enabled").toBool(true);
            m_avatarNameEdit->setText(m_avatarName);
            m_nameReactionCheckbox->setChecked(m_nameReactionEnabled);

            m_discordEnabledCheckbox->setChecked(obj.value("discord_enabled").toBool(false));
            m_discordBotTokenEdit->setText(obj.value("discord_bot_token").toString());
            m_discordChannelIdEdit->setText(obj.value("discord_channel_id").toString());

            m_obsHttpEnabledCheckbox->setChecked(obj.value("obs_http_enabled").toBool(false));
            m_obsHttpPortEdit->setText(QString::number(obj.value("obs_http_port").toInt(58082)));
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
    obj["twitch_username"] = m_twitchUsername;
    obj["webhook_url"] = m_webhookUrl;
    obj["webhook_enabled"] = m_webhookEnabled;
    obj["trans_cipher_key"] = obj.value("trans_cipher_key").toString("DefaultCipherKey123");

    m_avatarName = m_avatarNameEdit->text().trimmed();
    m_nameReactionEnabled = m_nameReactionCheckbox->isChecked();
    obj["avatar_name"] = m_avatarName;
    obj["name_reaction_enabled"] = m_nameReactionEnabled;

    obj["discord_enabled"] = m_discordEnabledCheckbox->isChecked();
    obj["discord_bot_token"] = m_discordBotTokenEdit->text().trimmed();
    obj["discord_channel_id"] = m_discordChannelIdEdit->text().trimmed();

    obj["obs_http_enabled"] = m_obsHttpEnabledCheckbox->isChecked();
    obj["obs_http_port"] = m_obsHttpPortEdit->text().trimmed().toInt();

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

            QRegularExpression reNew("const wsUri = \"ws://\" \\+ \\(window\\.location\\.hostname \\|\\| \"localhost\"\\) \\+ \":\\d+\";");
            QRegularExpression reOld("ws://localhost:\\d+");

            if (content.contains(reNew)) {
                content.replace(reNew, QString("const wsUri = \"ws://\" + (window.location.hostname || \"localhost\") + \":%1\";").arg(wsPort));
            } else {
                content.replace(reOld, QString("ws://localhost:%1").arg(wsPort));
            }

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

void AvatarWindow::enqueueRequest(const QString &text, const QString &user) {
    if (text.trimmed().isEmpty()) return;
    m_aiRequestQueue.enqueue(qMakePair(text.trimmed(), user));
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
    QPair<QString, QString> nextRequest = m_aiRequestQueue.dequeue();
    QString nextPrompt = nextRequest.first;
    QString user = nextRequest.second;
    qDebug() << "AvatarWindow: Processing next request:" << nextPrompt << "from user:" << user;

    pauseScheduler();
    updateAvatarDisplay("thinking");
    statusBar()->showMessage("AIの返答を待っています...");
    if (m_responseBrowser) {
        m_responseBrowser->setMarkdown("*AIの返答を待っています...*");
    }

    // AIクライアントへリクエストを要求するシグナルを発火
    emit requestAIExecution(nextPrompt, user);
}

void AvatarWindow::onCopyObsPathClicked() {
    if (m_obsPathEdit) {
        QString path = m_obsPathEdit->text();
        QGuiApplication::clipboard()->setText(path);
        statusBar()->showMessage("OBS用ファイルパスをクリップボードにコピーしました。");
    }
}

void AvatarWindow::initNicknameTab(QWidget *parent) {
    QVBoxLayout *mainLayout = new QVBoxLayout(parent);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);

    // 1. 登録済みユーザーニックネーム設定
    QGroupBox *usersGroup = new QGroupBox("登録済みユーザーの呼び名設定（ダブルクリックで優先呼び名を編集可能）", parent);
    QVBoxLayout *usersLayout = new QVBoxLayout(usersGroup);
    usersLayout->setContentsMargins(8, 8, 8, 8);
    usersLayout->setSpacing(6);

    m_usersTable = new QTableWidget(usersGroup);
    m_usersTable->setColumnCount(4);
    m_usersTable->setHorizontalHeaderLabels({"ユーザーID", "優先呼び名", "愛称リスト", "操作"});
    m_usersTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_usersTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_usersTable->setColumnWidth(3, 80);
    m_usersTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    
    // テーブルセル編集時の接続
    connect(m_usersTable, &QTableWidget::cellChanged, this, &AvatarWindow::onUserTableCellChanged);

    QHBoxLayout *usersBtnLayout = new QHBoxLayout();
    QPushButton *btnAddUser = new QPushButton("新規ユーザー追加", usersGroup);
    connect(btnAddUser, &QPushButton::clicked, this, &AvatarWindow::onAddUserClicked);
    usersBtnLayout->addWidget(btnAddUser);
    usersBtnLayout->addStretch();

    usersLayout->addWidget(m_usersTable);
    usersLayout->addLayout(usersBtnLayout);
    mainLayout->addWidget(usersGroup, 3);

    // 2. 承認待ちニックネーム登録リクエスト
    QGroupBox *requestsGroup = new QGroupBox("他者からの呼び名変更リクエスト（配信主による承認が必要）", parent);
    QVBoxLayout *requestsLayout = new QVBoxLayout(requestsGroup);
    requestsLayout->setContentsMargins(8, 8, 8, 8);
    requestsLayout->setSpacing(6);

    m_requestsTable = new QTableWidget(requestsGroup);
    m_requestsTable->setColumnCount(5);
    m_requestsTable->setHorizontalHeaderLabels({"申請者", "対象ユーザー", "提案された愛称", "申請日時", "操作"});
    m_requestsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_requestsTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_requestsTable->setColumnWidth(4, 160);
    m_requestsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_requestsTable->setSelectionBehavior(QAbstractItemView::SelectRows);

    requestsLayout->addWidget(m_requestsTable);
    mainLayout->addWidget(requestsGroup, 2);
}

void AvatarWindow::onNicknameDataUpdated(const QJsonObject &data) {
    m_cachedUserNamesData = data;
    updateNicknameTables();
}

void AvatarWindow::updateNicknameTables() {
    if (!m_usersTable || !m_requestsTable) return;

    // cellChanged シグナルを一時切断して、テーブル更新時の無限ループを防ぐ
    disconnect(m_usersTable, &QTableWidget::cellChanged, this, &AvatarWindow::onUserTableCellChanged);

    // 1. 登録済みユーザーテーブルの更新
    m_usersTable->setRowCount(0);
    QJsonObject usersMap = m_cachedUserNamesData.value("users").toObject();
    
    // キー（ユーザーID）をソート
    QStringList sortedUsers = usersMap.keys();
    sortedUsers.sort();

    m_usersTable->setRowCount(sortedUsers.size());
    for (int i = 0; i < sortedUsers.size(); ++i) {
        QString user = sortedUsers[i];
        QJsonObject userData = usersMap.value(user).toObject();
        QString preferred = userData.value("preferred").toString();
        QJsonArray nicknamesArray = userData.value("nicknames").toArray();
        QStringList nicknames;
        for (const QJsonValue &val : nicknamesArray) {
            nicknames.append(val.toString());
        }

        // ユーザーID (編集不可)
        QTableWidgetItem *itemUser = new QTableWidgetItem(user);
        itemUser->setFlags(itemUser->flags() & ~Qt::ItemIsEditable);
        m_usersTable->setItem(i, 0, itemUser);

        // 優先呼び名 (編集可能)
        QTableWidgetItem *itemPref = new QTableWidgetItem(preferred);
        m_usersTable->setItem(i, 1, itemPref);

        // 愛称リスト (編集不可)
        QTableWidgetItem *itemNicks = new QTableWidgetItem(nicknames.join(", "));
        itemNicks->setFlags(itemNicks->flags() & ~Qt::ItemIsEditable);
        m_usersTable->setItem(i, 2, itemNicks);

        // 削除ボタン
        QPushButton *btnDelete = new QPushButton("削除");
        btnDelete->setProperty("username", user);
        connect(btnDelete, &QPushButton::clicked, this, &AvatarWindow::onDeleteUserClicked);
        m_usersTable->setCellWidget(i, 3, btnDelete);
    }

    // cellChanged を再接続
    connect(m_usersTable, &QTableWidget::cellChanged, this, &AvatarWindow::onUserTableCellChanged);

    // 2. 承認待ちテーブルの更新
    m_requestsTable->setRowCount(0);
    QJsonArray pendingList = m_cachedUserNamesData.value("pending_requests").toArray();
    m_requestsTable->setRowCount(pendingList.size());
    
    for (int i = 0; i < pendingList.size(); ++i) {
        QJsonObject req = pendingList.at(i).toObject();
        QString requester = req.value("requester").toString();
        QString target = req.value("target").toString();
        QString nickname = req.value("nickname").toString();
        QString timestamp = req.value("timestamp").toString();

        m_requestsTable->setItem(i, 0, new QTableWidgetItem(requester));
        m_requestsTable->setItem(i, 1, new QTableWidgetItem(target));
        m_requestsTable->setItem(i, 2, new QTableWidgetItem(nickname));
        
        QDateTime dt = QDateTime::fromString(timestamp, Qt::ISODate);
        QString displayTime = dt.isValid() ? dt.toString("yyyy/MM/dd hh:mm") : timestamp;
        m_requestsTable->setItem(i, 3, new QTableWidgetItem(displayTime));

        // 操作ボタン
        QWidget *actionWidget = new QWidget(m_requestsTable);
        QHBoxLayout *actionLayout = new QHBoxLayout(actionWidget);
        actionLayout->setContentsMargins(2, 2, 2, 2);
        actionLayout->setSpacing(4);

        QPushButton *btnApprove = new QPushButton("許可", actionWidget);
        btnApprove->setProperty("requester", requester);
        btnApprove->setProperty("target", target);
        btnApprove->setProperty("nickname", nickname);
        connect(btnApprove, &QPushButton::clicked, this, &AvatarWindow::onApproveRequestClicked);

        QPushButton *btnReject = new QPushButton("却下", actionWidget);
        btnReject->setProperty("requester", requester);
        btnReject->setProperty("target", target);
        btnReject->setProperty("nickname", nickname);
        connect(btnReject, &QPushButton::clicked, this, &AvatarWindow::onRejectRequestClicked);

        actionLayout->addWidget(btnApprove);
        actionLayout->addWidget(btnReject);
        m_requestsTable->setCellWidget(i, 4, actionWidget);
    }
}

void AvatarWindow::onApproveRequestClicked() {
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    
    QString requester = btn->property("requester").toString();
    QString target = btn->property("target").toString();
    QString nickname = btn->property("nickname").toString();

    emit approveNicknameRequested(requester, target, nickname);
    statusBar()->showMessage(QString("リクエストを許可しました: %1 -> %2").arg(target).arg(nickname));
}

void AvatarWindow::onRejectRequestClicked() {
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;

    QString requester = btn->property("requester").toString();
    QString target = btn->property("target").toString();
    QString nickname = btn->property("nickname").toString();

    emit rejectNicknameRequested(requester, target, nickname);
    statusBar()->showMessage(QString("リクエストを却下しました: %1 -> %2").arg(target).arg(nickname));
}

void AvatarWindow::onDeleteUserClicked() {
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;

    QString username = btn->property("username").toString();
    
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "確認", QString("%1 さんのニックネーム設定を削除しますか？").arg(username),
        QMessageBox::Yes | QMessageBox::No
    );
    if (reply == QMessageBox::Yes) {
        emit deleteNicknameRequested(username);
        statusBar()->showMessage(QString("%1 さんのデータを削除しました。").arg(username));
    }
}

void AvatarWindow::onAddUserClicked() {
    bool ok;
    QString username = QInputDialog::getText(
        this, "ユーザー追加", "追加するユーザーのTwitch IDを入力してください:",
        QLineEdit::Normal, "", &ok
    );
    if (ok && !username.trimmed().isEmpty()) {
        QString userLower = username.trimmed().toLower();
        emit updateNicknamePreferredRequested(userLower, "");
        statusBar()->showMessage(QString("ユーザー「%1」を追加しました。ダブルクリックで優先呼び名を設定してください。").arg(userLower));
    }
}

void AvatarWindow::onUserTableCellChanged(int row, int column) {
    if (column != 1 || !m_usersTable) return;

    QTableWidgetItem *itemUser = m_usersTable->item(row, 0);
    QTableWidgetItem *itemPref = m_usersTable->item(row, 1);
    if (!itemUser || !itemPref) return;

    QString user = itemUser->text();
    QString preferred = itemPref->text().trimmed();

    emit updateNicknamePreferredRequested(user, preferred);
    statusBar()->showMessage(QString("%1 さんの優先呼び名を「%2」に更新しました。").arg(user).arg(preferred));
}

void AvatarWindow::initKnowledgeTab(QWidget *parent) {
    QVBoxLayout *layout = new QVBoxLayout(parent);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    m_knowledgeTable = new QTableWidget(parent);
    m_knowledgeTable->setColumnCount(5);
    m_knowledgeTable->setHorizontalHeaderLabels({"タイトル", "概要説明", "キーワード", "登録日時", "ID"});
    m_knowledgeTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_knowledgeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_knowledgeTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_knowledgeTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_knowledgeTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    layout->addWidget(m_knowledgeTable);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    m_deleteKnowledgeButton = new QPushButton("選択したナレッジを削除", parent);
    connect(m_deleteKnowledgeButton, &QPushButton::clicked, this, &AvatarWindow::onDeleteKnowledgeClicked);
    
    btnLayout->addStretch();
    btnLayout->addWidget(m_deleteKnowledgeButton);
    layout->addLayout(btnLayout);
}

void AvatarWindow::updateKnowledgeTable() {
    if (!m_knowledgeTable) return;
    
    m_knowledgeTable->setRowCount(0);
    QJsonArray knowledges = m_cachedKnowledgeData.value("knowledges").toArray();
    
    for (int i = 0; i < knowledges.size(); ++i) {
        QJsonObject entry = knowledges.at(i).toObject();
        QString id = entry.value("id").toString();
        QString title = entry.value("title").toString();
        QString description = entry.value("description").toString();
        
        QJsonArray kwArr = entry.value("keywords").toArray();
        QStringList kwList;
        for (const QJsonValue &v : kwArr) {
            kwList.append(v.toString());
        }
        QString keywords = kwList.join(", ");
        
        QString registeredAt = entry.value("registered_at").toString();

        m_knowledgeTable->insertRow(i);
        m_knowledgeTable->setItem(i, 0, new QTableWidgetItem(title));
        m_knowledgeTable->setItem(i, 1, new QTableWidgetItem(description));
        m_knowledgeTable->setItem(i, 2, new QTableWidgetItem(keywords));
        m_knowledgeTable->setItem(i, 3, new QTableWidgetItem(registeredAt));
        m_knowledgeTable->setItem(i, 4, new QTableWidgetItem(id));
    }
}

void AvatarWindow::onDeleteKnowledgeClicked() {
    if (!m_knowledgeTable) return;
    
    int row = m_knowledgeTable->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, "警告", "削除するナレッジを選択してください。");
        return;
    }
    
    QTableWidgetItem *itemId = m_knowledgeTable->item(row, 4);
    QTableWidgetItem *itemTitle = m_knowledgeTable->item(row, 0);
    if (!itemId || !itemTitle) return;
    
    QString id = itemId->text();
    QString title = itemTitle->text();
    
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "削除確認",
        QString("ナレッジ「%1」を本当に削除しますか？\n(実体ファイルも削除されます)").arg(title),
        QMessageBox::Yes | QMessageBox::No
    );
    
    if (reply == QMessageBox::Yes) {
        emit deleteKnowledgeRequested(id);
        statusBar()->showMessage(QString("ナレッジ「%1」の削除を要求しました。").arg(title));
    }
}

void AvatarWindow::onKnowledgeDataUpdated(const QJsonObject &data) {
    m_cachedKnowledgeData = data;
    updateKnowledgeTable();
}
