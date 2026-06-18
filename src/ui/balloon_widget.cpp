#include "balloon_widget.h"
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

BalloonWidget::BalloonWidget(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::SubWindow) 
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);

    // テキスト表示用ラベル
    m_textLabel = new QLabel(this);
    m_textLabel->setWordWrap(true);
    m_textLabel->setStyleSheet("color: black; font-size: 11pt; font-family: 'Segoe UI', Arial;");

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(15, 10, 15, 20); // 下部マージンを少し広くして吹き出し突起用の余白を確保
    layout->addWidget(m_textLabel);
    setLayout(layout);

    setMinimumSize(220, 80);
    setMaximumSize(350, 250);
}

BalloonWidget::~BalloonWidget() {
}

void BalloonWidget::showText(const QString &text) {
    m_textLabel->setText(text);
    adjustSize(); // テキスト長さに応じてウィジェットサイズを動的にリサイズ
    show();
}

void BalloonWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 吹き出しのメイン枠（角丸四角形）
    QRectF rect(0, 0, width(), height() - 12);
    QPainterPath path;
    path.addRoundedRect(rect, 10, 10);

    // 吹き出しの突起部分（左下にアバターがいるため、吹き出しの左下に尖りを描画）
    QPolygonF triangle;
    triangle << QPointF(20, height() - 12) 
             << QPointF(15, height()) 
             << QPointF(35, height() - 12);
    path.addPolygon(triangle);

    // 背景の塗りつぶし（高級感のある少し透過した白/薄いグレー）
    painter.fillPath(path, QColor(245, 245, 245, 240));

    // 枠線
    painter.setPen(QPen(QColor(180, 180, 180, 200), 1.5));
    painter.drawPath(path);
}
