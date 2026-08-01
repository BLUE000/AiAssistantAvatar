#ifndef RATE_LIMIT_TAB_WIDGET_H
#define RATE_LIMIT_TAB_WIDGET_H

#include <QWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QProgressBar>
#include <QSpinBox>
#include <QTimer>
#include <QList>
#include "../ai/rate_limit_tracker.h"

// 1つの動的制限項目 (RPM, RPD, TPM, Credit 等) の表示用カード
struct QuotaItemWidget {
    QLabel *nameLabel = nullptr;
    QProgressBar *progressBar = nullptr;
    QSpinBox *maxSpinBox = nullptr;
};

// 1つのプロバイダ用カード
struct ProviderCardWidget {
    QGroupBox *groupBox = nullptr;
    QLabel *statusLabel = nullptr;
    QLabel *keyStatusLabel = nullptr;
    QVBoxLayout *itemsLayout = nullptr;
    QList<QuotaItemWidget> itemWidgets;
};

class RateLimitTabWidget : public QWidget {
    Q_OBJECT
public:
    explicit RateLimitTabWidget(RateLimitTracker *tracker = nullptr, QWidget *parent = nullptr);
    virtual ~RateLimitTabWidget();

    void setTracker(RateLimitTracker *tracker) { m_tracker = tracker; refreshUI(); }

public slots:
    void refreshUI();

private:
    RateLimitTracker *m_tracker = nullptr;
    QTimer *m_updateTimer = nullptr;

    QVBoxLayout *m_cardsLayout = nullptr;
    QMap<QString, ProviderCardWidget> m_providerCards;

    void setupUI();
    void updateProviderCard(const ProviderStatus &status);
};

#endif // RATE_LIMIT_TAB_WIDGET_H
