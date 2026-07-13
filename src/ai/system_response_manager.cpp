#include "system_response_manager.h"
#include "version.h"
#include <QRegularExpression>

SystemResponseManager::SystemResponseManager(QObject *parent)
    : QObject(parent)
{
}

SystemResponseManager::~SystemResponseManager()
{
}

QString SystemResponseManager::processPrompt(const QString &prompt, const QString &currentProvider, const QString &avatarName) {
    QString trimmed = prompt.trimmed().toLower();
    if (trimmed.isEmpty()) return "";

    // アバター名（カスタム名）と言及キーワードの構築
    QString customAvatarPattern = "";
    if (!avatarName.isEmpty()) {
        customAvatarPattern = "|" + QRegularExpression::escape(avatarName.toLower());
    }
    QString avatarPrefix = "(アバター|あばたー|avatar|君|あなた|このアプリ|このシステム|このソフト" + customAvatarPattern + ")";

    // 1. バージョン情報の問い合わせ
    // - 単体での「version」などの入力
    // - または「アバターのバージョン」などの明確な修飾問い合わせ
    QRegularExpression verRegex(
        "(^version$|^バージョン$|^ばーじょん$|^versioninfo$|^バージョン情報$|"
        + avatarPrefix + "の(バージョン|version))",
        QRegularExpression::CaseInsensitiveOption
    );

    if (verRegex.match(trimmed).hasMatch()) {
        return QString("現在のバージョンは v%1 です。").arg(PROJECT_VERSION);
    }

    // 2. 使用中AI情報の問い合わせ
    // - 「アバターのAI」や「君が使っているAI」などアバターに関連した明確なAI問い合わせ
    QRegularExpression aiRegex(
        "(" + avatarPrefix + "の(ai|エーアイ)|"
        + avatarPrefix + "が(使っている|使用している|動いている|稼働している)ai)",
        QRegularExpression::CaseInsensitiveOption
    );

    if (aiRegex.match(trimmed).hasMatch()) {
        QString friendlyName = currentProvider;
        if (currentProvider == "mistral") friendlyName = "Mistral AI";
        else if (currentProvider == "groq") friendlyName = "Groq";
        else if (currentProvider == "cerebras") friendlyName = "Cerebras";
        else if (currentProvider == "dummy") friendlyName = "ダミーAIクライアント";
        else if (!friendlyName.isEmpty()) {
            friendlyName[0] = friendlyName[0].toUpper(); // 先頭大文字化
        }
        return QString("現在稼働しているAIは %1 です。").arg(friendlyName);
    }

    return "";
}
