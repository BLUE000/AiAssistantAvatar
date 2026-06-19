#pragma once
#include <QWidget>
#include <QLabel>
#include <QTimer>
#include <QVector>

class BalloonWidget : public QWidget {
    Q_OBJECT
private:
    QLabel *m_textLabel;
    QVector<QString> m_textPages;  // テキストのページ分割
    int m_currentPageIndex = 0;    // 現在表示中のページ
    QTimer *m_pageTimer = nullptr; // ページング用タイマー
    
    void updatePageDisplay();      // 現在のページを表示
    QVector<QString> splitTextIntoPages(const QString &text, int maxWidth, int maxHeight);

protected:
    void paintEvent(QPaintEvent *event) override;

public:
    explicit BalloonWidget(QWidget *parent = nullptr);
    ~BalloonWidget();

    void showText(const QString &text);

private slots:
    void onPageTimerTimeout();
};
