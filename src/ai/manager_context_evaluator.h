#pragma once
#include <QString>
#include <QList>
#include <QJsonObject>
#include <QDateTime>

struct ChatMessageEntry {
    QString messageId;
    QString sender;
    bool isAssistant = false;
    QString text;
    qint64 timestamp = 0;
};

struct ContextCandidate {
    QString messageId;
    QString sender;
    bool isAssistant = false;
    QString text;
    int ageSeconds = 0;
};

struct PendingClarification {
    QString requester;
    QString candidateTopic;
    QString questionText;
    qint64 timestamp = 0;

    bool isValid(qint64 currentMs = 0, qint64 timeoutMs = 60000) const {
        if (timestamp == 0 || questionText.isEmpty()) return false;
        if (currentMs == 0) currentMs = QDateTime::currentMSecsSinceEpoch();
        return (currentMs - timestamp) <= timeoutMs;
    }

    void clear() {
        requester.clear();
        candidateTopic.clear();
        questionText.clear();
        timestamp = 0;
    }
};

struct ManagerContextResult {
    QString target = "ASSISTANT";            // "ASSISTANT", "USER", "OTHER"
    QString speechAct = "QUESTION";          // "INFORMATION", "CORRECTION", "QUESTION", "COMMAND", "OPINION_DISAGREEMENT", "SUGGESTION", "REACTION", "OTHER"
    QString refMessageId;                    // 参照先メッセージID
    double referenceConfidence = 1.0;        // 0.0 〜 1.0
    QString responseAction = "ANSWER";       // "ANSWER", "ACKNOWLEDGE", "CORRECT_APOLOGY", "ASK_CLARIFICATION", "IGNORE"
    QString clarificationQuestion;           // 聞き返し文（ASK_CLARIFICATION 時）
};

class ManagerContextEvaluator {
public:
    static QList<ContextCandidate> extractContextCandidates(
        const QList<ChatMessageEntry> &chatLogs,
        const QString &currentPrompt,
        int maxCount = 5,
        qint64 currentMs = 0
    );

    static ManagerContextResult evaluateContextRuleBased(
        const QString &currentPrompt,
        const QString &sender,
        const QList<ContextCandidate> &candidates,
        const PendingClarification &pending,
        const QString &avatarName = "AIアシスタント"
    );

    static ManagerContextResult parseManagerJsonResponse(const QString &jsonStr);
    static QString buildManagerPrompt(
        const QString &currentPrompt,
        const QString &sender,
        const QList<ContextCandidate> &candidates,
        const PendingClarification &pending
    );

    static QString formatWorkerInstruction(
        const ManagerContextResult &result,
        const PendingClarification &pending
    );
};
