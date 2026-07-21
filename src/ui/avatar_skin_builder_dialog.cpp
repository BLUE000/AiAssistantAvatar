#include "avatar_skin_builder_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QPainter>
#include <QDebug>

AvatarSkinBuilderDialog::AvatarSkinBuilderDialog(QWidget *parent, const QString &editingSkinName)
    : QDialog(parent)
{
    setWindowTitle("アバタースキン作成・編集");
    resize(780, 580);

    // 初期状態の設定
    QStringList states = {"idle_front", "idle_back", "idle_right", "idle_left", "listening", "thinking", "speaking"};
    for (const QString &st : states) {
        SkinStateConfigUI cfg;
        m_stateConfigs[st] = cfg;
    }

    setupUI();

    if (!editingSkinName.isEmpty()) {
        loadExistingSkin(editingSkinName);
    }
}

void AvatarSkinBuilderDialog::setupUI() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // 上部：スキン名入力
    QHBoxLayout *topLayout = new QHBoxLayout();
    QLabel *lblSkinName = new QLabel("スキン名 (フォルダ名):", this);
    m_skinNameEdit = new QLineEdit(this);
    m_skinNameEdit->setPlaceholderText("例: MyCustomSkin");
    topLayout->addWidget(lblSkinName);
    topLayout->addWidget(m_skinNameEdit);
    mainLayout->addLayout(topLayout);

    // 中央：左側（設定フォーム）と右側（プレビュー）
    QHBoxLayout *contentLayout = new QHBoxLayout();

    // 左側：状態切り替えタブと編集フォーム
    QVBoxLayout *leftLayout = new QVBoxLayout();
    m_stateTabWidget = new QTabWidget(this);
    m_stateTabWidget->addTab(new QWidget(), "待機(正面)");
    m_stateTabWidget->addTab(new QWidget(), "待機(背面)");
    m_stateTabWidget->addTab(new QWidget(), "待機(右)");
    m_stateTabWidget->addTab(new QWidget(), "待機(左)");
    m_stateTabWidget->addTab(new QWidget(), "聴取中");
    m_stateTabWidget->addTab(new QWidget(), "思考中");
    m_stateTabWidget->addTab(new QWidget(), "発話中");

    leftLayout->addWidget(m_stateTabWidget);

    // 編集フォームコントロール
    QGroupBox *formGroup = new QGroupBox("選択された状態の設定", this);
    QFormLayout *formLayout = new QFormLayout(formGroup);

    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItems({"single", "random", "sequence"});

    m_singleFileEdit = new QLineEdit(this);
    m_browseSingleBtn = new QPushButton("参照...", this);
    QHBoxLayout *singleLayout = new QHBoxLayout();
    singleLayout->addWidget(m_singleFileEdit);
    singleLayout->addWidget(m_browseSingleBtn);

    m_filesListWidget = new QListWidget(this);
    m_filesListWidget->setFixedHeight(90);
    m_addFileBtn = new QPushButton("画像追加", this);
    m_removeFileBtn = new QPushButton("削除", this);
    QHBoxLayout *filesBtnLayout = new QHBoxLayout();
    filesBtnLayout->addWidget(m_addFileBtn);
    filesBtnLayout->addWidget(m_removeFileBtn);
    filesBtnLayout->addStretch();

    QVBoxLayout *multiLayout = new QVBoxLayout();
    multiLayout->addWidget(m_filesListWidget);
    multiLayout->addLayout(filesBtnLayout);

    m_frameIntervalSpin = new QSpinBox(this);
    m_frameIntervalSpin->setRange(10, 5000);
    m_frameIntervalSpin->setSuffix(" ms");
    m_frameIntervalSpin->setValue(150);

    m_durationSpin = new QSpinBox(this);
    m_durationSpin->setRange(100, 30000);
    m_durationSpin->setSuffix(" ms");
    m_durationSpin->setValue(1000);

    m_anchorXSpin = new QSpinBox(this);
    m_anchorXSpin->setRange(-1000, 2000);
    m_anchorXSpin->setValue(100);

    m_anchorYSpin = new QSpinBox(this);
    m_anchorYSpin->setRange(-1000, 2000);
    m_anchorYSpin->setValue(100);

    m_transparentXSpin = new QSpinBox(this);
    m_transparentXSpin->setRange(0, 2000);
    m_transparentXSpin->setValue(0);

    m_transparentYSpin = new QSpinBox(this);
    m_transparentYSpin->setRange(0, 2000);
    m_transparentYSpin->setValue(0);

    formLayout->addRow("表示モード:", m_modeCombo);
    formLayout->addRow("単一画像:", singleLayout);
    formLayout->addRow("複数画像リスト:", multiLayout);
    formLayout->addRow("コマ送り速度:", m_frameIntervalSpin);
    formLayout->addRow("状態表示時間:", m_durationSpin);

    QHBoxLayout *anchorLayout = new QHBoxLayout();
    anchorLayout->addWidget(new QLabel("X:"));
    anchorLayout->addWidget(m_anchorXSpin);
    anchorLayout->addWidget(new QLabel("Y:"));
    anchorLayout->addWidget(m_anchorYSpin);
    formLayout->addRow("アンカー座標:", anchorLayout);

    QHBoxLayout *transLayout = new QHBoxLayout();
    transLayout->addWidget(new QLabel("X:"));
    transLayout->addWidget(m_transparentXSpin);
    transLayout->addWidget(new QLabel("Y:"));
    transLayout->addWidget(m_transparentYSpin);
    formLayout->addRow("透過基準点:", transLayout);

    leftLayout->addWidget(formGroup);
    contentLayout->addLayout(leftLayout, 3);

    // 右側：プレビュー描画領域
    QVBoxLayout *rightLayout = new QVBoxLayout();
    QGroupBox *previewGroup = new QGroupBox("リアルタイムプレビュー", this);
    QVBoxLayout *previewInnerLayout = new QVBoxLayout(previewGroup);
    m_previewLabel = new QLabel(this);
    m_previewLabel->setMinimumSize(250, 250);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setStyleSheet("background-color: #2b2b2b; border: 1px solid #555;");
    previewInnerLayout->addWidget(m_previewLabel);
    rightLayout->addWidget(previewGroup);
    contentLayout->addLayout(rightLayout, 2);

    mainLayout->addLayout(contentLayout);

    // 下部：ボタン類
    QHBoxLayout *bottomBtnLayout = new QHBoxLayout();
    m_saveBtn = new QPushButton("自動生成して保存", this);
    m_saveBtn->setFixedHeight(32);
    m_cancelBtn = new QPushButton("キャンセル", this);
    m_cancelBtn->setFixedHeight(32);

    bottomBtnLayout->addStretch();
    bottomBtnLayout->addWidget(m_saveBtn);
    bottomBtnLayout->addWidget(m_cancelBtn);
    mainLayout->addLayout(bottomBtnLayout);

    // シグナル接続
    connect(m_stateTabWidget, &QTabWidget::currentChanged, this, [this](int index) {
        QStringList keys = {"idle_front", "idle_back", "idle_right", "idle_left", "listening", "thinking", "speaking"};
        if (index >= 0 && index < keys.size()) {
            syncConfigFromUI(m_currentStateKey);
            m_currentStateKey = keys[index];
            syncUIToConfig(m_currentStateKey);
        }
    });

    connect(m_modeCombo, &QComboBox::currentTextChanged, this, &AvatarSkinBuilderDialog::onModeChanged);
    connect(m_browseSingleBtn, &QPushButton::clicked, this, [this]() {
        QString file = QFileDialog::getOpenFileName(this, "画像ファイル選択", "", "画像 (*.png *.jpg *.jpeg *.bmp)");
        if (!file.isEmpty()) {
            m_singleFileEdit->setText(file);
            updatePreview();
        }
    });

    connect(m_addFileBtn, &QPushButton::clicked, this, &AvatarSkinBuilderDialog::onAddImageClicked);
    connect(m_removeFileBtn, &QPushButton::clicked, this, &AvatarSkinBuilderDialog::onRemoveImageClicked);
    connect(m_saveBtn, &QPushButton::clicked, this, &AvatarSkinBuilderDialog::onSaveClicked);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    connect(m_anchorXSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &AvatarSkinBuilderDialog::updatePreview);
    connect(m_anchorYSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &AvatarSkinBuilderDialog::updatePreview);
    connect(m_transparentXSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &AvatarSkinBuilderDialog::updatePreview);
    connect(m_transparentYSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &AvatarSkinBuilderDialog::updatePreview);

    // 初期化表示
    syncUIToConfig("idle_front");
}

void AvatarSkinBuilderDialog::onModeChanged(const QString &mode) {
    bool isSingle = (mode == "single");
    bool isSeq = (mode == "sequence");

    m_singleFileEdit->setEnabled(isSingle);
    m_browseSingleBtn->setEnabled(isSingle);
    m_filesListWidget->setEnabled(!isSingle);
    m_addFileBtn->setEnabled(!isSingle);
    m_removeFileBtn->setEnabled(!isSingle);
    m_frameIntervalSpin->setEnabled(isSeq);

    updatePreview();
}

void AvatarSkinBuilderDialog::onAddImageClicked() {
    QStringList files = QFileDialog::getOpenFileNames(this, "画像ファイル追加", "", "画像 (*.png *.jpg *.jpeg *.bmp)");
    for (const QString &f : files) {
        m_filesListWidget->addItem(f);
    }
    updatePreview();
}

void AvatarSkinBuilderDialog::onRemoveImageClicked() {
    int row = m_filesListWidget->currentRow();
    if (row >= 0) {
        delete m_filesListWidget->takeItem(row);
        updatePreview();
    }
}

void AvatarSkinBuilderDialog::syncUIToConfig(const QString &stateKey) {
    if (!m_stateConfigs.contains(stateKey)) return;
    const SkinStateConfigUI &cfg = m_stateConfigs[stateKey];

    m_modeCombo->setCurrentText(cfg.mode);
    m_singleFileEdit->setText(cfg.singleFile);
    m_filesListWidget->clear();
    for (const QString &f : cfg.files) {
        m_filesListWidget->addItem(f);
    }
    m_frameIntervalSpin->setValue(cfg.frameIntervalMs);
    m_durationSpin->setValue(cfg.durationMs);
    m_anchorXSpin->setValue(cfg.anchorX);
    m_anchorYSpin->setValue(cfg.anchorY);
    m_transparentXSpin->setValue(cfg.transparentX);
    m_transparentYSpin->setValue(cfg.transparentY);

    onModeChanged(cfg.mode);
}

void AvatarSkinBuilderDialog::syncConfigFromUI(const QString &stateKey) {
    if (!m_stateConfigs.contains(stateKey)) return;
    SkinStateConfigUI &cfg = m_stateConfigs[stateKey];

    cfg.mode = m_modeCombo->currentText();
    cfg.singleFile = m_singleFileEdit->text().trimmed();
    cfg.files.clear();
    for (int i = 0; i < m_filesListWidget->count(); ++i) {
        cfg.files.append(m_filesListWidget->item(i)->text());
    }
    cfg.frameIntervalMs = m_frameIntervalSpin->value();
    cfg.durationMs = m_durationSpin->value();
    cfg.anchorX = m_anchorXSpin->value();
    cfg.anchorY = m_anchorYSpin->value();
    cfg.transparentX = m_transparentXSpin->value();
    cfg.transparentY = m_transparentYSpin->value();
}

void AvatarSkinBuilderDialog::updatePreview() {
    QString filePath;
    QString mode = m_modeCombo->currentText();
    if (mode == "single") {
        filePath = m_singleFileEdit->text().trimmed();
    } else if (m_filesListWidget->count() > 0) {
        filePath = m_filesListWidget->item(0)->text();
    }

    if (filePath.isEmpty() || !QFile::exists(filePath)) {
        m_previewLabel->setText("画像未選択 / ファイルが存在しません");
        return;
    }

    QPixmap px(filePath);
    if (px.isNull()) {
        m_previewLabel->setText("画像の読み込みに失敗しました");
        return;
    }

    // アンカー位置を示す赤色十字線をプレビューにオーバレイ描画
    QPixmap previewPx = px.scaled(230, 230, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPainter painter(&previewPx);
    painter.setPen(QPen(Qt::red, 2));
    int cx = previewPx.width() / 2;
    int cy = previewPx.height() / 2;
    painter.drawLine(cx - 10, cy, cx + 10, cy);
    painter.drawLine(cx, cy - 10, cx, cy + 10);
    painter.end();

    m_previewLabel->setPixmap(previewPx);
}

void AvatarSkinBuilderDialog::loadExistingSkin(const QString &skinName) {
    m_skinNameEdit->setText(skinName);
    QString skinDir = "pic/" + skinName;
    QString jsonPath = skinDir + "/avatar_settings.json";

    if (!QFile::exists(jsonPath)) return;

    QFile file(jsonPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QByteArray data = file.readAll();
        file.close();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject root = doc.object();

            auto loadState = [&](const QJsonObject &obj, SkinStateConfigUI &cfg) {
                cfg.mode = obj["mode"].toString("single");
                cfg.singleFile = skinDir + "/" + obj["file"].toString();
                if (obj.contains("files") && obj["files"].isArray()) {
                    cfg.files.clear();
                    for (const QJsonValue &v : obj["files"].toArray()) {
                        cfg.files.append(skinDir + "/" + v.toString());
                    }
                }
                cfg.frameIntervalMs = obj["frame_interval_ms"].toInt(150);
                cfg.durationMs = obj["duration_ms"].toInt(1000);
                cfg.anchorX = obj["anchorX"].toInt(100);
                cfg.anchorY = obj["anchorY"].toInt(100);
                cfg.transparentX = obj["transparentX"].toInt(0);
                cfg.transparentY = obj["transparentY"].toInt(0);
            };

            if (root.contains("idle") && root["idle"].isObject()) {
                QJsonObject idleObj = root["idle"].toObject();
                if (idleObj.contains("front")) loadState(idleObj["front"].toObject(), m_stateConfigs["idle_front"]);
                if (idleObj.contains("back")) loadState(idleObj["back"].toObject(), m_stateConfigs["idle_back"]);
                if (idleObj.contains("right")) loadState(idleObj["right"].toObject(), m_stateConfigs["idle_right"]);
                if (idleObj.contains("left")) loadState(idleObj["left"].toObject(), m_stateConfigs["idle_left"]);
            }
            if (root.contains("listening")) loadState(root["listening"].toObject(), m_stateConfigs["listening"]);
            if (root.contains("thinking")) loadState(root["thinking"].toObject(), m_stateConfigs["thinking"]);
            if (root.contains("speaking")) loadState(root["speaking"].toObject(), m_stateConfigs["speaking"]);
        }
    }

    syncUIToConfig(m_currentStateKey);
}

void AvatarSkinBuilderDialog::onSaveClicked() {
    syncConfigFromUI(m_currentStateKey);

    QString skinName = generatedSkinName();
    if (skinName.isEmpty()) {
        QMessageBox::warning(this, "エラー", "スキン名（フォルダ名）を入力してください。");
        return;
    }

    if (generateSkinFiles()) {
        QMessageBox::information(this, "成功", QString("アバタースキン '%1' が正常に自動生成されました！").arg(skinName));
        accept();
    } else {
        QMessageBox::critical(this, "エラー", "スキンの生成に失敗しました。画像の参照パスを確認してください。");
    }
}

bool AvatarSkinBuilderDialog::generateSkinFiles() {
    QString skinName = generatedSkinName();
    QString targetDir = "pic/" + skinName;
    QDir().mkpath(targetDir);

    auto copyFileToSkin = [&](const QString &srcPath) -> QString {
        if (srcPath.isEmpty() || !QFile::exists(srcPath)) return "";
        QFileInfo info(srcPath);
        QString destPath = targetDir + "/" + info.fileName();
        if (srcPath != destPath) {
            QFile::remove(destPath);
            QFile::copy(srcPath, destPath);
        }
        return info.fileName();
    };

    // 画像をスキンジレクトリにコピーし、JSON構築
    QJsonObject rootObj;
    QJsonObject idleObj;
    idleObj["interval_ms"] = 15000;

    auto buildStateJson = [&](const SkinStateConfigUI &cfg) -> QJsonObject {
        QJsonObject obj;
        obj["mode"] = cfg.mode;
        obj["anchorX"] = cfg.anchorX;
        obj["anchorY"] = cfg.anchorY;
        obj["transparentX"] = cfg.transparentX;
        obj["transparentY"] = cfg.transparentY;

        if (cfg.mode == "single") {
            obj["file"] = copyFileToSkin(cfg.singleFile);
        } else {
            QJsonArray arr;
            for (const QString &f : cfg.files) {
                QString fn = copyFileToSkin(f);
                if (!fn.isEmpty()) arr.append(fn);
            }
            obj["files"] = arr;
            if (cfg.mode == "sequence") {
                obj["frame_interval_ms"] = cfg.frameIntervalMs;
            }
        }
        if (cfg.durationMs > 0) {
            obj["duration_ms"] = cfg.durationMs;
        }
        return obj;
    };

    idleObj["front"] = buildStateJson(m_stateConfigs["idle_front"]);
    idleObj["back"]  = buildStateJson(m_stateConfigs["idle_back"]);
    idleObj["right"] = buildStateJson(m_stateConfigs["idle_right"]);
    idleObj["left"]  = buildStateJson(m_stateConfigs["idle_left"]);
    rootObj["idle"]  = idleObj;

    rootObj["listening"] = buildStateJson(m_stateConfigs["listening"]);
    rootObj["thinking"]  = buildStateJson(m_stateConfigs["thinking"]);
    rootObj["speaking"]  = buildStateJson(m_stateConfigs["speaking"]);

    // avatar_settings.json 書き出し
    QFile jsonFile(targetDir + "/avatar_settings.json");
    if (jsonFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        jsonFile.write(QJsonDocument(rootObj).toJson());
        jsonFile.close();
    } else {
        return false;
    }

    // avatar_obs.html テンプレート出力
    QString htmlContent = R"(<!DOCTYPE html>
<html lang="ja">
<head>
<meta charset="UTF-8">
<title>Avatar OBS Stream Display</title>
<style>
  body { margin: 0; padding: 0; overflow: hidden; background: transparent; }
  #avatarContainer { position: absolute; }
  #avatarImage { display: block; }
</style>
</head>
<body>
<div id="avatarContainer">
  <img id="avatarImage" src="" alt="Avatar" />
</div>
<script>
  let ws;
  function connectWS() {
    ws = new WebSocket("ws://localhost:58081");
    ws.onmessage = function(event) {
      let data = JSON.parse(event.data);
      if (data.image) {
        document.getElementById("avatarImage").src = data.image;
      }
    };
    ws.onclose = function() { setTimeout(connectWS, 3000); };
  }
  connectWS();
</script>
</body>
</html>)";

    QFile htmlFile(targetDir + "/avatar_obs.html");
    if (htmlFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        htmlFile.write(htmlContent.toUtf8());
        htmlFile.close();
    }

    return true;
}
