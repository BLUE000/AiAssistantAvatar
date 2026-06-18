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
    QVector<FrontVariantEntry> entries;  // バリアントエントリ一覧
    int anchorX = 100;
    int anchorY = 100;
    int transparentX = 0;
    int transparentY = 0;
    int intervalMs = 5000;   // 切り替え間隔（ms）

    bool isEmpty() const { return entries.isEmpty(); }
};

// フレーム順に連続再生するアニメーションシーケンス
struct AnimationSequence {
    QString label;               // 表示名（メニュー用）
    QVector<QString> frames;     // ファイルパスの順序リスト
    int anchorX = 100;
    int anchorY = 100;
    int transparentX = 0;
    int transparentY = 0;
    int frameIntervalMs = 150;   // 1フレームあたりの表示時間（ms）
    bool loop = true;            // ループ再生するか
};

// UIのバルーン（吹き出し）ウィジェットを先行宣言
class BalloonWidget;

class AvatarWindow : public QMainWindow {
    Q_OBJECT
private:
    QLabel *m_avatarLabel;
    BalloonWidget *m_balloon;
    QMap<QString, ImageSetting> m_imageSettings;      // 状態ごとの設定
    QMap<QString, QPixmap> m_pixmapCache;             // 透過処理済みのキャッシュ
    QString m_currentState;                           // "idle", "thinking" 等
    QPoint m_desktopTargetPos;                        // アバター表示の基準目標座標
    QPoint m_dragPosition;                            // ドラッグ用一時座標

    // フロント画像ランダム切り替え用
    FrontVariantSettings m_frontVariants;             // フロントバリアント設定
    QVector<QPixmap> m_frontPixmapCache;              // 前処理済みバリアント画像
    QVector<int> m_weightCumulative;                  // 累積重みテーブル（重み付き選択用）
    int m_currentFrontIndex = 0;                      // 現在表示中のインデックス
    bool m_isFrontVariantMode = false;                // バリアントモード中フラグ
    QTimer *m_variantTimer = nullptr;                 // 切り替えタイマー

    // シーケンシャルアニメーション用
    QMap<QString, AnimationSequence> m_animations;         // アニメーション定義（名前 -> シーケンス）
    QMap<QString, QVector<QPixmap>> m_animPixmapCache;     // 各アニメーションのフレームキャッシュ
    QString m_currentAnimation;                            // 現在再生中のアニメーション名
    int m_animFrameIndex = 0;                              // 現在のフレームインデックス
    QTimer *m_animTimer = nullptr;                         // アニメーションフレーム切り替えタイマー
    
    void loadSettings();
    void processAndCacheImages();
    QPixmap applyTransparency(const QString &filePath, int tx, int ty);
    void updateAvatarDisplay(const QString &state);
    void updateWindowPosition();
    void switchToNextVariant();    // ランダムバリアント切り替え
    void playAnimation(const QString &name); // アニメーション再生開始
    void stepAnimationFrame();     // 次フレームを表示（タイマーから呼び出し）

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
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
