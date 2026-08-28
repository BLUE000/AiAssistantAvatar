#include "manager_context_evaluator.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QDebug>

QList<ContextCandidate> ManagerContextEvaluator::extractContextCandidates(
    const QList<ChatMessageEntry> &chatLogs,
    const QString &currentPrompt,
    int maxCount,
    qint64 currentMs
) {
    QList<ContextCandidate> candidates;
    if (chatLogs.isEmpty()) return candidates;

    if (currentMs == 0) {
        currentMs = QDateTime::currentMSecsSinceEpoch();
    }

    // 後ろ（最新）から探索して、一定条件（直近発言、AI発言、AI名を含む発言等）を満たすものを最大 maxCount 件抽出
    for (int i = chatLogs.size() - 1; i >= 0 && candidates.size() < maxCount; --i) {
        const auto &entry = chatLogs.at(i);
        ContextCandidate c;
        c.messageId = entry.messageId;
        c.sender = entry.sender;
        c.isAssistant = entry.isAssistant;
        c.text = entry.text;
        c.ageSeconds = static_cast<int>((currentMs - entry.timestamp) / 1000);
        if (c.ageSeconds < 0) c.ageSeconds = 0;

        candidates.prepend(c); // 時系列順（古い順）に整列
    }

    return candidates;
}

ManagerContextResult ManagerContextEvaluator::evaluateContextRuleBased(
    const QString &currentPrompt,
    const QString &sender,
    const QList<ContextCandidate> &candidates,
    const PendingClarification &pending,
    const QString &avatarName
) {
    ManagerContextResult result;
    result.target = "ASSISTANT";

    QString trimmed = currentPrompt.trimmed();

    // 1. 保留中の聞き返し（Pending Clarification）が存在する場合
    if (pending.isValid()) {
        result.speechAct = "ANSWER";
        result.responseAction = "ANSWER";
        result.referenceConfidence = 0.95;
        return result;
    }

    // 2. 挨拶・お礼等の代行発話指示の判定（例: 「配信終了のご挨拶をして」「挨拶して」「お礼を言って」）
    static const QRegularExpression greetOnBehalfRegex(
        "(?:(?:配信終了|終了|最後|締め|始まり|開始|オープニング|エンディング)?(?:の)?(?:ご挨拶|挨拶|お礼|ことば|メッセージ)(?:をして|お願い|して|を言って|述べて))",
        QRegularExpression::CaseInsensitiveOption
    );
    if (greetOnBehalfRegex.match(trimmed).hasMatch()) {
        result.speechAct = "COMMAND";
        result.responseAction = "GREET_ON_BEHALF";
        result.referenceConfidence = 0.95;
        return result;
    }

    // 3. 情報伝達の判定（例: 「〇〇さんが××だって言ってるよ」「〇〇さん曰く〜」「〇〇が配信終わるって」）
    static const QRegularExpression infoRegex(
        "(?:(?:さん|君|くん|ちゃん|氏)?(?:が|曰く|いわく|から).*?(?:言ってた|言ってる|伝えて|教えてくれた|とのこと|だって|らしいよ|終わるって|終了するって|行くって|来るって))",
        QRegularExpression::CaseInsensitiveOption
    );
    if (infoRegex.match(trimmed).hasMatch() && !trimmed.contains("？") && !trimmed.contains("?")) {
        result.speechAct = "INFORMATION";
        result.responseAction = "ACKNOWLEDGE";
        result.referenceConfidence = 0.90;
        return result;
    }

    // 4. 過去発言訂正・指摘の判定（例: 「そこは▼▼だよ」「それ違うよ」「違います」「間違ってるよ」「そうじゃなくて」）
    static const QRegularExpression correctionExplicitRegex(
        "(?:そこは|あれは|さっきの|前のは|それは|それ|そこ|あれ|違う|違います|まちが|間違|そうじゃない|じゃなくて)",
        QRegularExpression::CaseInsensitiveOption
    );

    if (correctionExplicitRegex.match(trimmed).hasMatch()) {
        // 指示語のみで対象が曖昧（例: 「それ違うよ」「そこ違う」）かつ候補が複数あるか
        bool hasAmbiguousDemonstrative = trimmed.contains("それ違う") || trimmed.contains("そこ違う") || trimmed.contains("それまちが") || trimmed.contains("違います");
        
        // 過去の AI 発言候補を探す
        int aiCandidateCount = 0;
        QString lastAiMsgId;
        for (const auto &c : candidates) {
            if (c.isAssistant) {
                aiCandidateCount++;
                lastAiMsgId = c.messageId;
            }
        }

        if (hasAmbiguousDemonstrative && (aiCandidateCount > 1 || candidates.size() > 1)) {
            // 候補が複数あり文脈が曖昧な場合 -> 参照先を特定せず聞き返し（安全性最優先）
            result.speechAct = "CORRECTION";
            result.responseAction = "ASK_CLARIFICATION";
            result.refMessageId = "";
            result.referenceConfidence = 0.40;
            result.clarificationQuestion = "それってどれのこと？";
            return result;
        }

        // 具体的な訂正内容が含まれている場合（例: 「そこは静岡だよ！」「富士山は静岡にもあるよ」）
        result.speechAct = "CORRECTION";
        result.responseAction = "CORRECT_APOLOGY";
        result.refMessageId = lastAiMsgId;
        result.referenceConfidence = 0.85;
        return result;
    }

    // 5. デフォルト判定
    result.speechAct = "QUESTION";
    result.responseAction = "ANSWER";
    result.referenceConfidence = 1.0;
    return result;
}

ManagerContextResult ManagerContextEvaluator::parseManagerJsonResponse(const QString &jsonStr) {
    ManagerContextResult result;
    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        // JSON 形式でない場合はデフォルトを返す
        return result;
    }

    QJsonObject obj = doc.object();
    if (obj.contains("target")) result.target = obj["target"].toString("ASSISTANT");
    if (obj.contains("speech_act")) result.speechAct = obj["speech_act"].toString("QUESTION");
    if (obj.contains("reference_message_id")) result.refMessageId = obj["reference_message_id"].toString();
    if (obj.contains("reference_confidence")) result.referenceConfidence = obj["reference_confidence"].toDouble(1.0);
    if (obj.contains("response_action")) result.responseAction = obj["response_action"].toString("ANSWER");
    if (obj.contains("clarification_question")) result.clarificationQuestion = obj["clarification_question"].toString();

    return result;
}

QString ManagerContextEvaluator::buildManagerPrompt(
    const QString &currentPrompt,
    const QString &sender,
    const QList<ContextCandidate> &candidates,
    const PendingClarification &pending
) {
    QJsonObject root;
    QJsonObject curObj;
    curObj["sender"] = sender;
    curObj["text"] = currentPrompt;
    root["current_message"] = curObj;

    QJsonArray candArr;
    for (const auto &c : candidates) {
        QJsonObject o;
        o["message_id"] = c.messageId;
        o["sender"] = c.sender;
        o["is_assistant"] = c.isAssistant;
        o["text"] = c.text;
        candArr.append(o);
    }
    root["candidates"] = candArr;

    if (pending.isValid()) {
        QJsonObject pendObj;
        pendObj["requester"] = pending.requester;
        pendObj["candidate_topic"] = pending.candidateTopic;
        pendObj["question_text"] = pending.questionText;
        root["pending_clarification"] = pendObj;
    } else {
        root["pending_clarification"] = QJsonValue::Null;
    }

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

QString ManagerContextEvaluator::formatWorkerInstruction(
    const ManagerContextResult &result,
    const PendingClarification &pending
) {
    if (pending.isValid()) {
        return QString(
            "【直前の聞き返し応答に対する文脈復元指示】\n"
            "直前にユーザーへ『%1』と確認質問を行いました。ユーザーの現在の発言はこの質問への回答です。\n"
            "直前の文脈を踏まえ、自然に理解して回答を行ってください。"
        ).arg(pending.questionText);
    }

    if (result.responseAction == "GREET_ON_BEHALF") {
        return QString(
            "【挨拶・発話の代行指示】\n"
            "ユーザーから配信終了の挨拶やお礼などの代行発話指示を受けました。\n"
            "発言者個人への労いではなく、配信の視聴者・全体に向けた挨拶（例: 『皆さん、本日の配信も見てくれてありがとうございました！また次回の配信でお会いしましょう！』）を明るく発話してください。"
        );
    }

    if (result.responseAction == "CORRECT_APOLOGY") {
        return QString(
            "【過去発言の訂正受容指示】\n"
            "ユーザーから過去のAI発言への訂正・指摘を受けました。\n"
            "一般論や人生論、励まし（周りの意見に合わせて改善していこう等）を展開することは完全に禁止します。\n"
            "素直に誤りを認めて『あ、〇〇なんだ！勘違いしてた、ごめん！』のように 1〜2 文程度で簡潔に返答してください。"
        );
    }

    if (result.responseAction == "ACKNOWLEDGE") {
        return QString(
            "【情報伝達の受け止め指示】\n"
            "ユーザーはAIへの情報伝達を行っています。質問として解説するのではなく、『へー、〇〇さんはそう言ってたんだ！』のように自然な相槌・リアクションを 1〜2 文で返答してください。『〜するって』などの未来・予告を『〜した』と過去形に誤認しないでください。"
        );
    }

    if (result.responseAction == "ASK_CLARIFICATION") {
        return QString(
            "【聞き返し指示】\n"
            "発言内容の参照先が不明です。『%1』のように 1 文で短く確認・聞き返しを行ってください。"
        ).arg(result.clarificationQuestion.isEmpty() ? "それってどれのこと？" : result.clarificationQuestion);
    }

    return QString();
}

