#pragma once
#include <QObject>
#include <QString>

class SystemResponseManager : public QObject {
    Q_OBJECT
public:
    explicit SystemResponseManager(QObject *parent = nullptr);
    ~SystemResponseManager();

    /**
     * @brief 入力プロンプトを判定し、固定応答があればそれを返す。なければ空文字列を返す。
     * @param prompt ユーザーの入力メッセージ
     */
    QString processPrompt(const QString &prompt, const QString &currentProvider, const QString &avatarName, const QString &currentModel = QString());
};
