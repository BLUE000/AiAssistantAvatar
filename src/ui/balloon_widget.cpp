#include "balloon_widget.h"
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QFontMetrics>
#include <QRegularExpression>

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
    setMaximumSize(400, 250);  // 最大サイズを固定
    
    // ページング用タイマーの設定
    m_pageTimer = new QTimer(this);
    connect(m_pageTimer, &QTimer::timeout, this, &BalloonWidget::onPageTimerTimeout);
}

BalloonWidget::~BalloonWidget() {
}

void BalloonWidget::showText(const QString &text) {
    // テキストを最大サイズに収まる単位でページに分割
    m_textPages = splitTextIntoPages(text, width() - 30, height() - 50);
    m_currentPageIndex = 0;
    
    // タイマーを停止
    if (m_pageTimer && m_pageTimer->isActive()) {
        m_pageTimer->stop();
    }
    
    // 最初のページを表示
    updatePageDisplay();
    show();
}

QVector<QString> BalloonWidget::splitTextIntoPages(const QString &text, int maxWidth, int maxHeight) {
    QVector<QString> pages;
    
    // デフォルトサイズを使用（ウィジェットがまだレイアウトされていない場合）
    if (maxWidth <= 0) maxWidth = 350;  // 最大サイズ - マージン
    if (maxHeight <= 0) maxHeight = 200; // 最大サイズ - マージン
    
    // テキストをテンポラリラベルで測定
    QLabel tempLabel;
    tempLabel.setWordWrap(true);
    tempLabel.setStyleSheet("color: black; font-size: 11pt; font-family: 'Segoe UI', Arial;");
    tempLabel.setMaximumWidth(maxWidth);
    tempLabel.setText(text);
    tempLabel.adjustSize();
    
    // テキストがバルーン内に収まる場合はそのまま返す
    if (tempLabel.height() <= maxHeight) {
        pages.append(text);
        return pages;
    }
    
    // テキストが収まらない場合は分割
    // 1行の高さを測定
    QLabel singleLineLabel;
    singleLineLabel.setWordWrap(true);
    singleLineLabel.setStyleSheet("color: black; font-size: 11pt; font-family: 'Segoe UI', Arial;");
    singleLineLabel.setMaximumWidth(maxWidth);
    singleLineLabel.setText("A");  // 1文字で行高を取得
    singleLineLabel.adjustSize();
    int lineHeight = singleLineLabel.height();
    
    if (lineHeight <= 0) lineHeight = 20;  // デフォルト値
    
    // 1ページに表示できる最大行数（安全マージン）
    int maxLinesPerPage = qMax(1, (maxHeight - 20) / lineHeight);
    
    // テキストを単語ごとに分割（スペース区切り）
    QStringList words = text.split(' ', Qt::KeepEmptyParts);
    QString currentPage;
    int totalWords = words.size();
    
    for (int i = 0; i < totalWords; ++i) {
        const QString &word = words[i];
        
        // 単語を追加
        QString testPage = currentPage + (currentPage.isEmpty() ? "" : " ") + word;
        QLabel testLabel;
        testLabel.setWordWrap(true);
        testLabel.setStyleSheet("color: black; font-size: 11pt; font-family: 'Segoe UI', Arial;");
        testLabel.setMaximumWidth(maxWidth);
        testLabel.setText(testPage);
        testLabel.adjustSize();
        
        int estimatedLines = (testLabel.height() + lineHeight - 1) / lineHeight;
        
        if (estimatedLines >= maxLinesPerPage && !currentPage.isEmpty()) {
            // 現在のページを保存して新しいページを開始
            pages.append(currentPage + "...");
            currentPage = word;
        } else {
            if (currentPage.isEmpty()) {
                currentPage = word;
            } else {
                currentPage += " " + word;
            }
        }
    }
    
    // 最後のページを追加
    if (!currentPage.isEmpty()) {
        pages.append(currentPage.trimmed());
    }
    
    return pages.isEmpty() ? QVector<QString>{text} : pages;
}

void BalloonWidget::updatePageDisplay() {
    if (m_textPages.isEmpty()) return;
    
    // 現在のページを表示
    m_textLabel->setText(m_textPages[m_currentPageIndex]);
    adjustSize();
    
    // ページが残っている場合はタイマーを設定
    if (m_currentPageIndex < m_textPages.size() - 1) {
        if (m_pageTimer) {
            m_pageTimer->start(3000);  // 3秒後に次のページを表示
        }
    } else {
        if (m_pageTimer && m_pageTimer->isActive()) {
            m_pageTimer->stop();
        }
    }
}

void BalloonWidget::onPageTimerTimeout() {
    if (m_currentPageIndex < m_textPages.size() - 1) {
        m_currentPageIndex++;
        updatePageDisplay();
    }
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
