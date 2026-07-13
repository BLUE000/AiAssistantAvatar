#include "system_response_manager.h"
#include "version.h"

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

    // アバター・システムへの言及ワードがあるか
    bool mentionsAvatar = false;
    if (!avatarName.isEmpty() && trimmed.contains(avatarName.toLower())) {
        mentionsAvatar = true;
    }
    if (trimmed.contains("アバター") || trimmed.contains("あばたー") || trimmed.contains("avatar") ||
        trimmed.contains("君") || trimmed.contains("あなた") || trimmed.contains("このアプリ") ||
        trimmed.contains("このシステム") || trimmed.contains("このソフト") || trimmed.contains("本体")) {
        mentionsAvatar = true;
    }

    // 1. バージョン情報の判定 (複数キーワード AND 判定と、無関係ワードの排除)
    bool hasVersionKeyword = (trimmed.contains("version") || trimmed.contains("バージョン") || trimmed.contains("ばーじょん") || trimmed.contains("versioninfo"));
    if (hasVersionKeyword) {
        // バージョン単体、またはアバターへの言及がある場合
        bool isVersionTarget = (trimmed == "version" || trimmed == "バージョン" || trimmed == "ばーじょん" || trimmed == "versioninfo" || mentionsAvatar);
        
        // 他の対象（マイクラ、ゲーム、Windowsなど）のバージョンを尋ねるものではないことを確認
        bool hasOtherContext = (trimmed.contains("ゲーム") || trimmed.contains("マイクラ") || trimmed.contains("windows") ||
                               trimmed.contains("mac") || trimmed.contains("os") || trimmed.contains("python") ||
                               trimmed.contains("qt") || trimmed.contains("obs") || trimmed.contains("discord") || trimmed.contains("twitch"));
        
        if (isVersionTarget && !hasOtherContext) {
            return QString("現在のバージョンは v%1 です。").arg(PROJECT_VERSION);
        }
    }

    // 2. 使用中AI情報の判定 (複数キーワード AND 判定と、無関係ワードの排除)
    bool hasAIKeyword = (trimmed.contains("ai") || trimmed.contains("エーアイ") || trimmed.contains("モデル") || trimmed.contains("プロバイダ"));
    if (hasAIKeyword && mentionsAvatar) {
        // 接続・使用を示す動詞または所有格の確認
        bool hasUsageKeyword = (trimmed.contains("使っている") || trimmed.contains("使用している") || 
                               trimmed.contains("動いている") || trimmed.contains("稼働している") || 
                               trimmed.contains("動かしている") || trimmed.contains("のai") || 
                               trimmed.contains("のモデル") || trimmed.contains("のプロバイダ"));
        
        // 無関係な雑談や他の製品に関するものではないか
        bool hasIrrelevantKeyword = (trimmed.contains("おすすめ") || trimmed.contains("比較") || trimmed.contains("未来") || trimmed.contains("chatgpt") || trimmed.contains("openai"));

        if (hasUsageKeyword && !hasIrrelevantKeyword) {
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
    }

    return "";
}
