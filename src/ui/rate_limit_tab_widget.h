#ifndef RATE_LIMIT_TAB_WIDGET_H
#define RATE_LIMIT_TAB_WIDGET_H

#include <QWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <QMap>
#include <QList>
#include "../ai/rate_limit_tracker.h"

// 1つの管理項目 (RPM / RPD) の再利用用UI構造体
struct QuotaRowWidget {
    QWidget *container = nullptr;
    QLabel *label = nullptr;
    QProgressBar *progressBar = nullptr;
};

// 1つのプロバイダ用カード構造体
struct ProviderCardWidget {
    QGroupBox *groupBox = nullptr;
    QLabel *statusLabel = nullptr;
    QLabel *keyStatusLabel = nullptr;
    QVBoxLayout *itemsLayout = nullptr;
    QuotaRowWidget rpmRow;
    QuotaRowWidget rpdRow;
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
