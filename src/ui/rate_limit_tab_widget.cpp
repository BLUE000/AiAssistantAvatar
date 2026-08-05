#include "rate_limit_tab_widget.h"
#include <QDateTime>
#include <QShowEvent>
#include <QDebug>

RateLimitTabWidget::RateLimitTabWidget(QWidget *parent)
    : QWidget(parent) {
    setupUI();
}

void RateLimitTabWidget::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    emit requestRefreshStatus();
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

    // 初期ラベル（シグナル受信前の空状態表示）
    m_cardsLayout->addWidget(new QLabel("⏳ AI プロバイダのステータスを待機中...", scrollContent));
    m_cardsLayout->addStretch();
}

void RateLimitTabWidget::onStatusUpdated(const QList<ProviderStatus> &statuses) {
    // 初回受信時に待機ラベルを消す
    if (!m_providerCards.isEmpty() == false) {
        // 待機ラベルとストレッチを除去
        while (QLayoutItem *item = m_cardsLayout->takeAt(0)) {
            if (QWidget *w = item->widget()) {
                w->deleteLater();
            }
            delete item;
        }
    }

    for (const auto &st : statuses) {
        updateProviderCard(st);
    }
}

void RateLimitTabWidget::updateProviderCard(const ProviderStatus &status) {
    QString id = status.provider;
    // DUMMY プロバイダは表示対象から除外
    if (id.isEmpty() || id.toLower() == "dummy") return;

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

        // RPM 行の初回生成
        {
            card.rpmRow.container = new QWidget(card.groupBox);
            QHBoxLayout *row = new QHBoxLayout(card.rpmRow.container);
            row->setContentsMargins(0, 0, 0, 0);

            card.rpmRow.label = new QLabel(card.rpmRow.container);
            card.rpmRow.progressBar = new QProgressBar(card.rpmRow.container);
            card.rpmRow.progressBar->setTextVisible(false);
            card.rpmRow.progressBar->setFixedHeight(14);

            row->addWidget(card.rpmRow.label);
            row->addWidget(card.rpmRow.progressBar, 1);
            card.itemsLayout->addWidget(card.rpmRow.container);
            card.rpmRow.container->setVisible(false);
        }

        // RPD 行の初回生成
        {
            card.rpdRow.container = new QWidget(card.groupBox);
            QHBoxLayout *row = new QHBoxLayout(card.rpdRow.container);
            row->setContentsMargins(0, 0, 0, 0);

            card.rpdRow.label = new QLabel(card.rpdRow.container);
            card.rpdRow.progressBar = new QProgressBar(card.rpdRow.container);
            card.rpdRow.progressBar->setTextVisible(false);
            card.rpdRow.progressBar->setFixedHeight(14);

            row->addWidget(card.rpdRow.label);
            row->addWidget(card.rpdRow.progressBar, 1);
            card.itemsLayout->addWidget(card.rpdRow.container);
            card.rpdRow.container->setVisible(false);
        }

        gbLayout->addLayout(card.itemsLayout);

        m_cardsLayout->addWidget(card.groupBox);
        m_providerCards.insert(id, card);
    }

    ProviderCardWidget &card = m_providerCards[id];

    // 1. キー設定状態の描画
    card.keyStatusLabel->setText("🔑 APIキー: 設定済み");
    card.keyStatusLabel->setStyleSheet("color: #2e7d32; font-weight: bold;");

    // 2. レートリミットステータス ＆ カウントダウン描画
    QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    bool isLimited = !status.available || (status.rpmMax > 0 && status.rpmRemaining <= 0);
    qint64 waitSec = 0;

    if (status.nextResetAt.isValid() && status.nextResetAt > nowUtc) {
        waitSec = nowUtc.secsTo(status.nextResetAt);
    }

    // 残り比率判定（残り30%未満で 🟡 もうすぐ上限）
    double remainingRatio = 1.0;
    if (status.rpmMax > 0 && status.rpmRemaining >= 0) {
        remainingRatio = static_cast<double>(status.rpmRemaining) / status.rpmMax;
    }

    if (isLimited) {
        qint64 min = waitSec / 60;
        qint64 sec = waitSec % 60;
        card.statusLabel->setText(QString("🔴 レートリミット到達中 (解除まで あと %1分%2秒)")
                                      .arg(min, 2, 10, QChar('0'))
                                      .arg(sec, 2, 10, QChar('0')));
        card.statusLabel->setStyleSheet("color: #d32f2f; font-weight: bold;");
    } else if (remainingRatio < 0.3 && status.rpmRemaining > 0) {
        card.statusLabel->setText(QString("🟡 もうすぐ上限 (残り %1 回)").arg(status.rpmRemaining));
        card.statusLabel->setStyleSheet("color: #ef6c00; font-weight: bold;");
    } else {
        card.statusLabel->setText("🟢 利用可能");
        card.statusLabel->setStyleSheet("color: #2e7d32; font-weight: bold;");
    }

    // 3. 動的管理項目 (RPM / RPD) の安全更新（再利用方式でメモリ破棄・生成を行わない）
    if (status.rpmMax > 0) {
        int rpmUsed = (status.rpmRemaining >= 0) ? (status.rpmMax - status.rpmRemaining) : 0;
        rpmUsed = qBound(0, rpmUsed, status.rpmMax);

        card.rpmRow.label->setText(QString("1分使用枠 (RPM): %1 / %2 回").arg(rpmUsed).arg(status.rpmMax));
        card.rpmRow.progressBar->setRange(0, status.rpmMax);
        card.rpmRow.progressBar->setValue(rpmUsed);

        if (isLimited || rpmUsed >= status.rpmMax) {
            card.rpmRow.progressBar->setStyleSheet("QProgressBar::chunk { background-color: #e53935; }");
        } else if (remainingRatio < 0.3) {
            card.rpmRow.progressBar->setStyleSheet("QProgressBar::chunk { background-color: #fb8c00; }");
        } else {
            card.rpmRow.progressBar->setStyleSheet("QProgressBar::chunk { background-color: #43a047; }");
        }
        card.rpmRow.container->setVisible(true);
    } else {
        card.rpmRow.container->setVisible(false);
    }


    if (status.rpdMax > 0) {
        int rpdUsed = (status.rpdRemaining >= 0) ? (status.rpdMax - status.rpdRemaining) : 0;
        rpdUsed = qBound(0, rpdUsed, status.rpdMax);

        card.rpdRow.label->setText(QString("1日使用枠 (RPD): %1 / %2 回").arg(rpdUsed).arg(status.rpdMax));
        card.rpdRow.progressBar->setRange(0, status.rpdMax);
        card.rpdRow.progressBar->setValue(rpdUsed);

        if (rpdUsed >= status.rpdMax) {
            card.rpdRow.progressBar->setStyleSheet("QProgressBar::chunk { background-color: #e53935; }");
        } else {
            card.rpdRow.progressBar->setStyleSheet("QProgressBar::chunk { background-color: #1e88e5; }");
        }
        card.rpdRow.container->setVisible(true);
    } else {
        card.rpdRow.container->setVisible(false);
    }
}
