#pragma once
#include <QObject>
#include <QString>

class ISearchProvider : public QObject {
    Q_OBJECT
public:
    explicit ISearchProvider(QObject *parent = nullptr);
    virtual ~ISearchProvider();
    virtual void search(const QString &query) = 0;

    // 検索結果スニペットのクレンジング＆サニタイズ (F-48)
    static QString cleanseSnippet(const QString &text, int maxChars = 350);

signals:
    void searchFinished(const QString &resultText, bool success);
};
