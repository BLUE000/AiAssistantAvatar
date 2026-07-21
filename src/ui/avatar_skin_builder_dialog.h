#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QListWidget>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QTabWidget>
#include <QJsonObject>
#include <QMap>

struct SkinStateConfigUI {
    QString mode = "single"; // single, random, sequence
    QString singleFile;
    QStringList files;
    int frameIntervalMs = 150;
    int durationMs = 1000;
    int anchorX = 100;
    int anchorY = 100;
    int transparentX = 0;
    int transparentY = 0;
};

class AvatarSkinBuilderDialog : public QDialog {
    Q_OBJECT
public:
    explicit AvatarSkinBuilderDialog(QWidget *parent = nullptr, const QString &editingSkinName = "");
    ~AvatarSkinBuilderDialog() override = default;

    QString generatedSkinName() const { return m_skinNameEdit ? m_skinNameEdit->text().trimmed() : ""; }

private slots:
    void onAddImageClicked();
    void onRemoveImageClicked();
    void onModeChanged(const QString &mode);
    void onSaveClicked();
    void updatePreview();

private:
    QLineEdit *m_skinNameEdit = nullptr;
    QTabWidget *m_stateTabWidget = nullptr;
    
    // 現在選択中の状態UIコントロール
    QMap<QString, SkinStateConfigUI> m_stateConfigs;
    QString m_currentStateKey = "idle_front";

    // ダイアログ内のUIコントロール
    QComboBox *m_modeCombo = nullptr;
    QLineEdit *m_singleFileEdit = nullptr;
    QPushButton *m_browseSingleBtn = nullptr;
    QListWidget *m_filesListWidget = nullptr;
    QPushButton *m_addFileBtn = nullptr;
    QPushButton *m_removeFileBtn = nullptr;
    QSpinBox *m_frameIntervalSpin = nullptr;
    QSpinBox *m_durationSpin = nullptr;
    QSpinBox *m_anchorXSpin = nullptr;
    QSpinBox *m_anchorYSpin = nullptr;
    QSpinBox *m_transparentXSpin = nullptr;
    QSpinBox *m_transparentYSpin = nullptr;

    QLabel *m_previewLabel = nullptr;
    QPushButton *m_saveBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;

    void setupUI();
    void loadExistingSkin(const QString &skinName);
    void syncUIToConfig(const QString &stateKey);
    void syncConfigFromUI(const QString &stateKey);
    bool generateSkinFiles();
};
