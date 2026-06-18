#pragma once
#include <QWidget>
#include <QLabel>

class BalloonWidget : public QWidget {
    Q_OBJECT
private:
    QLabel *m_textLabel;

protected:
    void paintEvent(QPaintEvent *event) override;

public:
    explicit BalloonWidget(QWidget *parent = nullptr);
    ~BalloonWidget();

    void showText(const QString &text);
};
