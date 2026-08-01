#include "rate_limit_tab_widget.h"
#include <QDateTime>
#include <QDebug>

RateLimitTabWidget::RateLimitTabWidget(RateLimitTracker *tracker, QWidget *parent)
    : QWidget(parent), m_tracker(tracker) {
    setupUI();

    m_updateTimer = new QTimer(this);
    connect(m_updateTimer, &QTimer::timeout, this, &RateLimitTabWidget::refreshUI);
    m_updateTimer->start(1000); // 1秒間隔でリアルタイムカウントダウン・残枠を更新
}

RateLimitTabWidget::~RateLimitTabWidget() {
    if (m_updateTimer) {
        m_updateTimer->stop();
    }
}

void RateLimitTabWidget::setupUI() {
    QVBoxLayout *containerLayout = new QVBoxLayout(this);
    containerLayout->setContentsMargins(0, 0, 0, 0);

    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    QWidget *scrollContent = new QWidget(scrollArea);
    m_cardsLayout = new QVBoxLayout(scrollContent);
    m_cardsLayout->setContentsMargins(10, 10, 10, 10);
    m_cardsLayout->setSpacing(10);

    scrollArea->setWidget(scrollContent);
    containerLayout->addWidget(scrollArea);

    refreshUI();
}

void RateLimitTabWidget::refreshUI() {
    if (!m_tracker) return;

    QList<ProviderStatus> statuses = m_tracker->allStatuses();
    for (const auto &st : statuses) {
        updateProviderCard(st);
    }
}

void RateLimitTabWidget::updateProviderCard(const ProviderStatus &status) {
    QString id = status.provider;
    if (id.isEmpty()) return;

    bool exists = m_providerCards.contains(id);
    if (!exists) {
        ProviderCardWidget card;
        card.groupBox = new QGroupBox(id.toUpper() + " プロバイダ", this);
        QVBoxLayout *gbLayout = new QVBoxLayout(card.groupBox);
        gbLayout->setContentsMargins(10, 10, 10, 10);
        gbLayout->setSpacing(6);

        QHBoxLayout *headerLayout = new QHBoxLayout();
        card.keyStatusLabel = new QLabel(card.groupBox);
        card.statusLabel = new QLabel(card.groupBox);
        headerLayout->addWidget(card.keyStatusLabel);
        headerLayout->addStretch();
        headerLayout->addWidget(card.statusLabel);

        gbLayout->addLayout(headerLayout);

        card.itemsLayout = new QVBoxLayout();
        card.itemsLayout->setSpacing(4);
        gbLayout->addLayout(card.itemsLayout);

        m_cardsLayout->addWidget(card.groupBox);
        m_providerCards.insert(id, card);
    }

    ProviderCardWidget &card = m_providerCards[id];

    // 1. キー設定状態の描画
    card.keyStatusLabel->setText("🔑 APIキー: 設定済み");
    card.keyStatusLabel->setStyleSheet("color: #2e7d32; font-weight: bold;");

    // 2. レートリミットステータス ＆ カウントダウン描画
    QDateTime now = QDateTime::currentDateTime();
    bool isLimited = !status.available;
    qint64 waitSec = 0;

    if (status.nextResetAt.isValid() && status.nextResetAt > now) {
        waitSec = now.secsTo(status.nextResetAt);
    }

    if (isLimited) {
        qint64 min = waitSec / 60;
        qint64 sec = waitSec % 60;
        card.statusLabel->setText(QString("🔴 レートリミット到達中 (解除まで あと %1分%2秒)")
                                      .arg(min, 2, 10, QChar('0'))
                                      .arg(sec, 2, 10, QChar('0')));
        card.statusLabel->setStyleSheet("color: #d32f2f; font-weight: bold;");
    } else {
        card.statusLabel->setText("🟢 利用可能");
        card.statusLabel->setStyleSheet("color: #2e7d32; font-weight: bold;");
    }

    // 3. 動的管理項目 (RPM / RPD) のプログレスバー化 描画 (曖昧表示 N/A や - は全廃)
    while (QLayoutItem *item = card.itemsLayout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->setParent(nullptr);
            delete w;
        }
        delete item;
    }
    card.itemWidgets.clear();

    // RPM 項目が存在する場合のみ描画 (仕様上存在しない項目は描画せず隠す)
    if (status.rpmMax > 0) {
        QWidget *w = new QWidget(card.groupBox);
        QHBoxLayout *row = new QHBoxLayout(w);
        row->setContentsMargins(0, 0, 0, 0);

        int rpmUsed = (status.rpmRemaining >= 0) ? (status.rpmMax - status.rpmRemaining) : 0;
        rpmUsed = qBound(0, rpmUsed, status.rpmMax);

        QLabel *lbl = new QLabel(QString("1分使用枠 (RPM): %1 / %2 回").arg(rpmUsed).arg(status.rpmMax), w);
        QProgressBar *pb = new QProgressBar(w);
        pb->setRange(0, status.rpmMax);
        pb->setValue(rpmUsed);
        pb->setTextVisible(false);
        pb->setFixedHeight(14);

        if (rpmUsed >= status.rpmMax) {
            pb->setStyleSheet("QProgressBar::chunk { background-color: #e53935; }");
        } else {
            pb->setStyleSheet("QProgressBar::chunk { background-color: #43a047; }");
        }

        row->addWidget(lbl);
        row->addWidget(pb, 1);
        card.itemsLayout->addWidget(w);
    }

    // RPD 項目が存在する場合のみ描画
    if (status.rpdMax > 0) {
        QWidget *w = new QWidget(card.groupBox);
        QHBoxLayout *row = new QHBoxLayout(w);
        row->setContentsMargins(0, 0, 0, 0);

        int rpdUsed = (status.rpdRemaining >= 0) ? (status.rpdMax - status.rpdRemaining) : 0;
        rpdUsed = qBound(0, rpdUsed, status.rpdMax);

        QLabel *lbl = new QLabel(QString("1日使用枠 (RPD): %1 / %2 回").arg(rpdUsed).arg(status.rpdMax), w);
        QProgressBar *pb = new QProgressBar(w);
        pb->setRange(0, status.rpdMax);
        pb->setValue(rpdUsed);
        pb->setTextVisible(false);
        pb->setFixedHeight(14);

        if (rpdUsed >= status.rpdMax) {
            pb->setStyleSheet("QProgressBar::chunk { background-color: #e53935; }");
        } else {
            pb->setStyleSheet("QProgressBar::chunk { background-color: #1e88e5; }");
        }

        row->addWidget(lbl);
        row->addWidget(pb, 1);
        card.itemsLayout->addWidget(w);
    }
}
