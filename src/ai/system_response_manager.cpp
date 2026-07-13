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

QString SystemResponseManager::processPrompt(const QString &prompt) {
    QString trimmed = prompt.trimmed().toLower();

    // バージョンに関する問い合わせパターン（正規表現）
    QRegularExpression versionRegex(
        "(^version$|^バージョン$|^ばーじょん$|バージョンは|バージョンを教えて|今のバージョン|現在のバージョン|versioninfo|バージョン情報)",
        QRegularExpression::CaseInsensitiveOption
    );

    if (versionRegex.match(trimmed).hasMatch()) {
        return QString("現在のバージョンは v%1 です。").arg(PROJECT_VERSION);
    }

    return "";
}
