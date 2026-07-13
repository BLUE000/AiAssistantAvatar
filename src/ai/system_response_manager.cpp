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

    // プレフィックスなしでの「version」「バージョン」単体一致も許容
    bool isPlainVersionCmd = (trimmed == "version" || trimmed == "バージョン" || trimmed == "ばーじょん" || trimmed == "versioninfo");

    // 呼びかけも単体コマンドもなければ、自動応答しない（通常のAIに任せる）
    if (!mentionsAvatar && !isPlainVersionCmd) {
        return "";
    }

    // 1. バージョン情報の判定
    if (trimmed.contains("version") || trimmed.contains("バージョン") || trimmed.contains("ばーじょん")) {
        // 「〇〇のバージョン」という修飾語があるかチェック
        QRegularExpression modifierRegex("([a-zA-Z0-9_\\x{4e00}-\\x{9fa5}]+)の(バージョン|version)");
        QRegularExpressionMatch match = modifierRegex.match(trimmed);
        
        bool isOwnVersion = true;
        if (match.hasMatch()) {
            QString modifier = match.captured(1);
            // 修飾語がアバターやシステム自身を指しているか？
            bool isSelfModifier = false;
            if (!avatarName.isEmpty() && modifier == avatarName.toLower()) isSelfModifier = true;
            if (modifier == "アバター" || modifier == "あばたー" || modifier == "avatar" ||
                modifier == "君" || modifier == "あなた" || modifier == "このアプリ" ||
                modifier == "このシステム" || modifier == "このソフト" || modifier == "本体" ||
                modifier == "システム") {
                isSelfModifier = true;
            }
            // 自分以外（マイクラ、Windowsなど無関係な名詞）の修飾語がある場合は、他者のバージョンとみなす
            if (!isSelfModifier) {
                isOwnVersion = false;
            }
        }

        if (isOwnVersion) {
            return QString("現在のバージョンは v%1 です。").arg(PROJECT_VERSION);
        }
    }

    // 2. 使用中AI情報の判定（アバターへの呼びかけが必須）
    if (mentionsAvatar && (trimmed.contains("ai") || trimmed.contains("エーアイ") || trimmed.contains("モデル") || trimmed.contains("プロバイダ"))) {
        // 「〇〇のai」という修飾語があるか
        QRegularExpression modifierRegex("([a-zA-Z0-9_\\x{4e00}-\\x{9fa5}]+)の(ai|エーアイ|モデル|プロバイダ)");
        QRegularExpressionMatch match = modifierRegex.match(trimmed);
        
        bool isOwnAI = true;
        if (match.hasMatch()) {
            QString modifier = match.captured(1);
            bool isSelfModifier = false;
            if (!avatarName.isEmpty() && modifier == avatarName.toLower()) isSelfModifier = true;
            if (modifier == "アバター" || modifier == "あばたー" || modifier == "avatar" ||
                modifier == "君" || modifier == "あなた" || modifier == "このアプリ" ||
                modifier == "このシステム" || modifier == "このソフト" || modifier == "本体" ||
                modifier == "システム") {
                isSelfModifier = true;
            }
            if (!isSelfModifier) {
                isOwnAI = false;
            }
        }

        // 「使っている」「使用している」などの接続語があるか、あるいは「〇〇のAI」で自分自身のAIであることが確定しているか
        bool hasUsageWord = (trimmed.contains("使っている") || trimmed.contains("使用している") || 
                             trimmed.contains("動いている") || trimmed.contains("稼働している") || 
                             trimmed.contains("動かしている") || trimmed.contains("のai") || 
                             trimmed.contains("のモデル") || trimmed.contains("のプロバイダ"));

        if (isOwnAI && hasUsageWord) {
            // 他社サービス名などが明記されている場合は、一般的な雑談とみなし除外
            bool hasOtherAI = (trimmed.contains("chatgpt") || trimmed.contains("openai") || trimmed.contains("gemini") || trimmed.contains("claude"));
            if (!hasOtherAI) {
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
    }

    return "";
}
