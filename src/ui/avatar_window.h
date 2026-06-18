#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QPixmap>
#include <QPoint>
#include <QMap>
#include "../app_event.h"

struct ImageSetting {
    QString filePath;
    int anchorX = 0;
    int anchorY = 0;
    int transparentX = 0;
    int transparentY = 0;
};

// UIのバルーン（吹き出し）ウィジェットを先行宣言
class BalloonWidget;

class AvatarWindow : public QMainWindow {
    Q_OBJECT
private:
    QLabel *m_avatarLabel;
    BalloonWidget *m_balloon;
    QMap<QString, ImageSetting> m_imageSettings; // 状態ごとの設定
    QMap<QString, QPixmap> m_pixmapCache;        // 透過処理済みのキャッシュ
    QString m_currentState;                      // "idle", "thinking" 等
    QPoint m_desktopTargetPos;                   // アバター表示の基準目標座標
    QPoint m_dragPosition;                       // ドラッグ用一時座標
    
    void loadSettings();
    void processAndCacheImages();
    QPixmap applyTransparency(const QString &filePath, int tx, int ty);
    void updateAvatarDisplay(const QString &state);
    void updateWindowPosition();

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

public slots:
    // コアから通知を受け取るスロット
    void on_notify_events(const AppEvent &event);
};
