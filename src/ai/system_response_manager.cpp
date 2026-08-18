#include "system_response_manager.h"
#include "ai_random_utils.h"
#include "version.h"
#include <QRegularExpression>

SystemResponseManager::SystemResponseManager(QObject *parent)
    : QObject(parent)
{
}

SystemResponseManager::~SystemResponseManager()
{
}

QString SystemResponseManager::processPrompt(const QString &prompt, const QString &currentProvider, const QString &avatarName, const QString &currentModel) {
    QString trimmed = prompt.trimmed();
    if (trimmed.isEmpty()) return "";

    // 0. Random / RandomList 単体コマンドの判定
    if (trimmed.startsWith("Random(", Qt::CaseInsensitive) || trimmed.startsWith("RandomList(", Qt::CaseInsensitive)) {
        QString evaluated = AIRandomUtils::parseAndEvaluate(trimmed);
        if (!evaluated.isEmpty() && evaluated != trimmed) {
            return QString("ランダム結果: %1").arg(evaluated);
        }
    }

    QString lowerTrimmed = trimmed.toLower();

    // アバター・システムへの言及ワードがあるか
    bool mentionsAvatar = false;
    if (!avatarName.isEmpty() && lowerTrimmed.contains(avatarName.toLower())) {
        mentionsAvatar = true;
    }
    if (lowerTrimmed.contains("アバター") || lowerTrimmed.contains("あばたー") || lowerTrimmed.contains("avatar") ||
        lowerTrimmed.contains("君") || lowerTrimmed.contains("あなた") || lowerTrimmed.contains("このアプリ") ||
        lowerTrimmed.contains("このシステム") || lowerTrimmed.contains("このソフト") || lowerTrimmed.contains("本体")) {
        mentionsAvatar = true;
    }

    // プレフィックスなしでの「version」「バージョン」単体一致も許容
    bool isPlainVersionCmd = (lowerTrimmed == "version" || lowerTrimmed == "バージョン" || lowerTrimmed == "ばーじょん" || lowerTrimmed == "versioninfo");
    bool isPlainRateLimitCmd = lowerTrimmed.contains("レートリミット") || lowerTrimmed.contains("リミット") || lowerTrimmed == "/ratelimit" || lowerTrimmed == "/status";

    // 呼びかけも単体コマンドもなければ、自動応答しない（通常のAIに任せる）
    if (!mentionsAvatar && !isPlainVersionCmd && !isPlainRateLimitCmd) {
        return "";
    }


    // 1. バージョン情報の判定
    if (lowerTrimmed.contains("version") || lowerTrimmed.contains("バージョン") || lowerTrimmed.contains("ばーじょん")) {
        // 「〇〇のバージョン」という修飾語があるかチェック
        QRegularExpression modifierRegex("([a-zA-Z0-9_\\x{4e00}-\\x{9fa5}]+)の(バージョン|version)");
        QRegularExpressionMatch match = modifierRegex.match(lowerTrimmed);
        
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
    if (mentionsAvatar && (lowerTrimmed.contains("ai") || lowerTrimmed.contains("エーアイ") || lowerTrimmed.contains("モデル") || lowerTrimmed.contains("プロバイダ"))) {
        // 「〇〇のai」という修飾語があるか
        QRegularExpression modifierRegex("([a-zA-Z0-9_\\x{4e00}-\\x{9fa5}]+)の(ai|エーアイ|モデル|プロバイダ)");
        QRegularExpressionMatch match = modifierRegex.match(lowerTrimmed);
        
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
        bool hasUsageWord = (lowerTrimmed.contains("使っている") || lowerTrimmed.contains("使用している") || 
                             lowerTrimmed.contains("動いている") || lowerTrimmed.contains("稼働している") || 
                             lowerTrimmed.contains("動かしている") || lowerTrimmed.contains("のai") || 
                             lowerTrimmed.contains("のモデル") || lowerTrimmed.contains("のプロバイダ"));

        if (isOwnAI && hasUsageWord) {
            // 他社サービス名などが明記されている場合は、一般的な雑談とみなし除外
            bool hasOtherAI = (lowerTrimmed.contains("chatgpt") || lowerTrimmed.contains("openai") || lowerTrimmed.contains("gemini") || lowerTrimmed.contains("claude"));
            if (!hasOtherAI) {
                QString friendlyName = currentProvider;
                if (currentProvider == "huggingface") friendlyName = "Hugging Face";
                else if (currentProvider == "openrouter") friendlyName = "OpenRouter";
                else if (currentProvider == "sakura") friendlyName = "さくらAI";
                else if (currentProvider == "mistral") friendlyName = "Mistral AI";
                else if (currentProvider == "groq") friendlyName = "Groq";
                else if (currentProvider == "dummy") friendlyName = "ダミーAIクライアント";
                else if (!friendlyName.isEmpty()) {
                    friendlyName[0] = friendlyName[0].toUpper(); // 先頭大文字化
                }

                if (!currentModel.isEmpty()) {
                    return QString("現在稼働しているAIは %1 (モデル: %2) です。").arg(friendlyName, currentModel);
                }
                return QString("現在稼働しているAIは %1 です。").arg(friendlyName);
            }
        }
    }

    // 3. レートリミット表示・更新問い合わせの判定
    if (lowerTrimmed.contains("レートリミット") || lowerTrimmed.contains("リミット") || lowerTrimmed.contains("制限")) {
        if (lowerTrimmed.contains("更新") || lowerTrimmed.contains("表示") || lowerTrimmed.contains("教えて") ||
            lowerTrimmed.contains("見せて") || lowerTrimmed.contains("どう") || lowerTrimmed.contains("確認") ||
            lowerTrimmed.contains("チェック") || lowerTrimmed == "/ratelimit" || lowerTrimmed == "/status") {
            return "レートリミット情報を更新しました。「レートリミット」タブから各AIプロバイダの利用枠や残量、解除までのカウントダウンをご確認いただけます。";
        }
    }

    return "";
}

