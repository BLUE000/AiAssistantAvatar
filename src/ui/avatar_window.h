#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QPixmap>
#include <QPoint>
#include <QMap>
#include <QVector>
#include <QTimer>
#include "../app_event.h"

struct ImageSetting {
    QString filePath;
    int anchorX = 0;
    int anchorY = 0;
    int transparentX = 0;
    int transparentY = 0;
};

struct FrontVariantEntry {
    QString filePath;  // 画像ファイルパス
    int weight = 1;    // 出現重み（大きいほど出やすい）
};

struct FrontVariantSettings {
    QString label;               // 表示名（メニュー用）
    QVector<FrontVariantEntry> entries;
    int anchorX = 100;
    int anchorY = 100;
    int transparentX = 0;
    int transparentY = 0;
    int intervalMs = 5000;

    bool isEmpty() const { return entries.isEmpty(); }
};

// フレーム順に連続再生するアニメーションシーケンス
struct AnimationSequence {
    QString label;
    QVector<QString> frames;
    int anchorX = 100;
    int anchorY = 100;
    int transparentX = 0;
    int transparentY = 0;
    int frameIntervalMs = 150;
    bool loop = true;
};

// パターンスケジューラーの1エントリ
struct PatternSchedulerEntry {
    QString type;       // "variant_group" または "animation"
    QString name;       // グループ名 / アニメーション名
    int weight = 1;     // 選択重み
    int stayMs = 8000;  // variant_group の場合の滞在時間（ms）
};

// UIのバルーン（吹き出し）ウィジェットを先行宣言
class BalloonWidget;
class QLineEdit;
class QPushButton;

class AvatarWindow : public QMainWindow {
    Q_OBJECT
private:
    QLabel *m_avatarLabel;
    BalloonWidget *m_balloon;
    QLineEdit *m_inputEdit = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_sttButton = nullptr;
    QPushButton *m_menuButton = nullptr;

    QMap<QString, ImageSetting> m_imageSettings;      // 状態ごとの設定
    QMap<QString, QPixmap> m_pixmapCache;             // 透過処理済みのキャッシュ
    QString m_currentState;                           // "idle", "thinking" 等
    QPoint m_desktopTargetPos;                        // アバター表示の基準目標座標
    QPoint m_dragPosition;                            // ドラッグ用一時座標
    QPoint m_lastWindowPos;                           // ドラッグ後の最後のウィンドウ位置を保存
    bool m_userDraggedWindow = false;                 // ユーザーがドラッグで移動したかどうかのフラグ

    // バリアントグループ（front_variants / back_variants 等）の汎用管理
    QMap<QString, FrontVariantSettings> m_allVariantGroups;   // 全グループ定義
    QMap<QString, QVector<QPixmap>> m_allVariantCaches;       // 全グループのキャッシュ
    QMap<QString, QVector<int>> m_allVariantWeights;          // 全グループの累積重み
    QString m_activeVariantGroupName;                         // 現在アクティブなグループ名
    int m_currentFrontIndex = 0;         // 現在表示中のインデックス
    bool m_isFrontVariantMode = false;   // バリアントモード中フラグ
    QTimer *m_variantTimer = nullptr;    // 切り替えタイマー

    // シーケンシャルアニメーション用
    QMap<QString, AnimationSequence> m_animations;
    QMap<QString, QVector<QPixmap>> m_animPixmapCache;
    QString m_currentAnimation;
    int m_animFrameIndex = 0;
    QTimer *m_animTimer = nullptr;
    bool m_animAutoPlay = false;  // スケジューラー自動再生中フラグ

    // パターンスケジューラー
    QVector<PatternSchedulerEntry> m_schedulerEntries;
    QVector<int> m_schedulerWeights;
    QString m_lastScheduledName;
    QTimer *m_schedulerTimer = nullptr;
    bool m_schedulerEnabled = false;
    bool m_schedulerPaused = false;   // AI 応答中は一時停止
    QTimer *m_resumeTimer = nullptr;  // Speaking 後に自動再開するタイマー
    void loadSettings();
    void processAndCacheImages();
    QPixmap applyTransparency(const QString &filePath, int tx, int ty);
    void updateAvatarDisplay(const QString &state);
    void updateWindowPosition();
    void switchToNextVariant();
    void switchVariantGroup(const QString &groupName);
    void playAnimation(const QString &name, bool autoPlay = false);
    void stepAnimationFrame();
    void pickNextPattern();
    void pauseScheduler();    // AI 処理中にスケジューラーを停止
    void resumeScheduler();   // AI 処理完了後にスケジューラーを再開
    void showContextMenu(const QPoint &globalPos);

private slots:
    void onSendClicked();
    void onSttClicked();
    void onMenuClicked();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

public:
    explicit AvatarWindow(QWidget *parent = nullptr);
    ~AvatarWindow();

signals:
    // コアスレッドへの要求シグナル
    void startSTTRequested();
    void directInputSubmitted(const QString &text);
    void resetSessionRequested(); // 会話履歴リセット要求シグナル
    void importSessionRequested(const QString &filePath);
    void exportSessionRequested(const QString &encPath, const QString &txtPath);

public slots:
    // コアから通知を受け取るスロット
    void on_notify_events(const AppEvent &event);
};
