#include "community_observer_engine.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QDebug>

QJsonObject ObserverEvaluationResult::toJson() const {
    QJsonObject obj;
    obj["status"] = statusString;
    obj["user"] = user;
    obj["platform"] = platform;
    obj["history_count"] = historyCount;
    obj["usual_topics"] = usualTopics;
    obj["usual_sentiment"] = usualSentiment;
    obj["current_topic"] = currentTopic;
    obj["current_sentiment"] = currentSentiment;
    obj["concern_level"] = concernLevel;
    obj["anomaly_summary"] = anomalySummary;
    obj["directive"] = directive;
    return obj;
}

ObserverEvaluationResult ObserverEvaluationResult::fromJson(const QJsonObject &obj) {
    ObserverEvaluationResult res;
    res.statusString = obj.value("status").toString("Normal");
    if (res.statusString == "DrasticChange") {
        res.status = ObserverStatus::DrasticChange;
    } else if (res.statusString == "PersistentConcern") {
        res.status = ObserverStatus::PersistentConcern;
    } else {
        res.status = ObserverStatus::Normal;
    }
    res.user = obj.value("user").toString();
    res.platform = obj.value("platform").toString("twitch");
    res.historyCount = obj.value("history_count").toInt();
    res.usualTopics = obj.value("usual_topics").toString();
    res.usualSentiment = obj.value("usual_sentiment").toString();
    res.currentTopic = obj.value("current_topic").toString();
    res.currentSentiment = obj.value("current_sentiment").toString();
    res.concernLevel = obj.value("concern_level").toInt();
    res.anomalySummary = obj.value("anomaly_summary").toString();
    res.directive = obj.value("directive").toString();
    return res;
}

QJsonObject UserMessageRecord::toJson() const {
    QJsonObject obj;
    obj["timestamp"] = timestamp.toUTC().toString(Qt::ISODate);
    obj["text"] = text;
    obj["topic"] = topic;
    obj["tone"] = tone;
    return obj;
}

UserMessageRecord UserMessageRecord::fromJson(const QJsonObject &obj) {
    UserMessageRecord rec;
    rec.timestamp = QDateTime::fromString(obj.value("timestamp").toString(), Qt::ISODate);
    rec.text = obj.value("text").toString();
    rec.topic = obj.value("topic").toString();
    rec.tone = obj.value("tone").toString();
    return rec;
}

CommunityObserverEngine::CommunityObserverEngine()
    : m_logsDirectory("Config/observer_logs")
{
}

CommunityObserverEngine::CommunityObserverEngine(const QString &baseDir)
    : m_logsDirectory(baseDir)
{
}

void CommunityObserverEngine::setLogsDirectory(const QString &dirPath) {
    m_logsDirectory = dirPath;
}

QString CommunityObserverEngine::logsDirectory() const {
    return m_logsDirectory;
}

QString CommunityObserverEngine::resolveLogFilePath(const QString &platform, const QString &user) const {
    QString cleanUser = user.trimmed().toLower();
    cleanUser.replace(QRegularExpression("[^a-zA-Z0-9_\\-\\.]"), "_");
    if (cleanUser.isEmpty()) cleanUser = "unknown";

    QString cleanPlatform = platform.trimmed().toLower();
    if (cleanPlatform.isEmpty()) cleanPlatform = "twitch";

    return QString("%1/%2_%3.json").arg(m_logsDirectory, cleanPlatform, cleanUser);
}

QList<UserMessageRecord> CommunityObserverEngine::loadUserRecords(const QString &filePath) const {
    QList<UserMessageRecord> list;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return list;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return list;

    QJsonArray arr = doc.object().value("records").toArray();
    for (const QJsonValue &val : arr) {
        if (val.isObject()) {
            list.append(UserMessageRecord::fromJson(val.toObject()));
        }
    }
    return list;
}

bool CommunityObserverEngine::saveUserRecords(const QString &filePath, const QString &platform, const QString &user, const QList<UserMessageRecord> &records) const {
    QFileInfo fi(filePath);
    QDir().mkpath(fi.absolutePath());

    QJsonObject rootObj;
    rootObj["user"] = user;
    rootObj["platform"] = platform;
    rootObj["total_messages"] = records.size();
    if (!records.isEmpty()) {
        rootObj["first_seen"] = records.first().timestamp.toUTC().toString(Qt::ISODate);
        rootObj["last_seen"] = records.last().timestamp.toUTC().toString(Qt::ISODate);
    }

    QJsonArray arr;
    for (const auto &rec : records) {
        arr.append(rec.toJson());
    }
    rootObj["records"] = arr;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QJsonDocument doc(rootObj);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

QString CommunityObserverEngine::classifyTopic(const QString &text) {
    QString t = text.toLower();
    // 不満・愚痴・投げやり
    if (t.contains("だるい") || t.contains("無理") || t.contains("疲れた") ||
        t.contains("消えろ") || t.contains("嫌い") || t.contains("うざい") ||
        t.contains("ウザい") || t.contains("イライラ") || t.contains("腹立つ") ||
        t.contains("苦手") || t.contains("どうでもいい") || t.contains("やりたくない") ||
        t.contains("やってられない") || t.contains("消したい")) {
        return "complaint_venting";
    }

    // ゲーム関連
    if (t.contains("ゲーム") || t.contains("キャラ") || t.contains("ボス") ||
        t.contains("クエスト") || t.contains("ランク") || t.contains("マッチ") ||
        t.contains("タンク") || t.contains("サポート") || t.contains("ダメージ") ||
        t.contains("エイム") || t.contains("play") || t.contains("overwatch") ||
        t.contains("apex") || t.contains("valorant") || t.contains("minecraft")) {
        return "game";
    }

    // 人間関係・他者言及
    if (t.contains("さん") || t.contains("くん") || t.contains("ちゃん") ||
        t.contains("あいつ") || t.contains("リスナー") || t.contains("視聴者") ||
        t.contains("配信者") || t.contains("みんな")) {
        return "interpersonal";
    }

    return "casual_chat";
}

QString CommunityObserverEngine::classifyTone(const QString &text) {
    QString t = text.toLower();
    // 強い敵意・強い棘
    if (t.contains("消したい") || t.contains("消えろ") || t.contains("死ね") ||
        t.contains("本当に無理") || t.contains("マジで嫌い") || t.contains("生理的に無理") ||
        t.contains("来ないで") || t.contains("関わりたくない") || t.contains("大嫌い")) {
        return "negative_strong";
    }

    // 軽い愚痴・不満
    if (t.contains("苦手") || t.contains("疲れた") || t.contains("だるい") ||
        t.contains("うざい") || t.contains("ウザい") || t.contains("イライラ") ||
        t.contains("無理") || t.contains("どうでもいい") || t.contains("腹立つ")) {
        return "negative_mild";
    }

    // 肯定・ポジティブ
    if (t.contains("草") || t.contains("w") || t.contains("笑") ||
        t.contains("面白い") || t.contains("好き") || t.contains("ありがとう") ||
        t.contains("楽しい") || t.contains("最高") || t.contains("すごい") ||
        t.contains("かわいい") || t.contains("かっこいい") || t.contains("神")) {
        return "positive";
    }

    return "neutral";
}

bool CommunityObserverEngine::recordMessage(const QString &platform, const QString &user, const QString &text) {
    if (user.trimmed().isEmpty() || text.trimmed().isEmpty()) {
        return false;
    }

    QString filePath = resolveLogFilePath(platform, user);
    QList<UserMessageRecord> records = loadUserRecords(filePath);

    UserMessageRecord newRec;
    newRec.timestamp = QDateTime::currentDateTimeUtc();
    newRec.text = text.trimmed();
    newRec.topic = classifyTopic(newRec.text);
    newRec.tone = classifyTone(newRec.text);

    records.append(newRec);

    // 最大100件に制限
    while (records.size() > 100) {
        records.removeFirst();
    }

    return saveUserRecords(filePath, platform, user, records);
}

ObserverEvaluationResult CommunityObserverEngine::evaluateMessage(const QString &platform, const QString &user, const QString &text) {
    ObserverEvaluationResult result;
    result.user = user.trimmed();
    result.platform = platform.trimmed().isEmpty() ? "twitch" : platform.trimmed();
    result.currentTopic = classifyTopic(text);
    result.currentSentiment = classifyTone(text);
    result.status = ObserverStatus::Normal;
    result.statusString = "Normal";
    result.concernLevel = 0;
    result.directive = "";

    if (result.user.isEmpty() || text.trimmed().isEmpty()) {
        return result;
    }

    QString filePath = resolveLogFilePath(platform, user);
    QList<UserMessageRecord> records = loadUserRecords(filePath);
    result.historyCount = records.size();

    // 過去ログがない初回〜極少時は通常判定
    if (records.size() < 3) {
        result.usualTopics = result.currentTopic;
        result.usualSentiment = "neutral";
        return result;
    }

    // 過去ログの傾向集計
    int posCount = 0;
    int neuCount = 0;
    int negMildCount = 0;
    int negStrongCount = 0;
    int gameCount = 0;
    int chatCount = 0;

    for (const auto &rec : records) {
        if (rec.tone == "positive") posCount++;
        else if (rec.tone == "neutral") neuCount++;
        else if (rec.tone == "negative_mild") negMildCount++;
        else if (rec.tone == "negative_strong") negStrongCount++;

        if (rec.topic == "game") gameCount++;
        else if (rec.topic == "casual_chat") chatCount++;
    }

    double friendlyRatio = (double)(posCount + neuCount) / (double)records.size();
    result.usualTopics = (gameCount >= chatCount) ? "game, casual_chat" : "casual_chat, general";
    result.usualSentiment = (posCount >= neuCount) ? "positive" : "neutral";

    // 1. 継続的懸念 (PersistentConcern) の判定
    // 直近5件中2件以上、または過去ログで複数回のネガティブ発言がある状態で、今回もネガティブ
    int recentNegCount = 0;
    int recentCheckCount = qMin(5, records.size());
    for (int i = records.size() - recentCheckCount; i < records.size(); ++i) {
        if (records[i].tone == "negative_mild" || records[i].tone == "negative_strong") {
            recentNegCount++;
        }
    }

    if ((recentNegCount >= 2 || (negMildCount + negStrongCount) >= 4) &&
        (result.currentSentiment == "negative_mild" || result.currentSentiment == "negative_strong")) {
        result.status = ObserverStatus::PersistentConcern;
        result.statusString = "PersistentConcern";
        result.concernLevel = 3;
        result.anomalySummary = "特定対象や話題に対する不満・強い感情が複数回にわたり継続して蓄積";
        result.directive = "[対話誘導指示: ユーザーから過去の文脈でも同様の話題について強い感情や不満が継続して見られます。一方的に決めつけや説教をせず、『前にも気にしてたみたいだけど、何かあった？』『無理してない？』のように、状況の説明を優しく促す聞き返しを行ってください。医療的判断は行わず、友人として寄り添う姿勢を維持してください。]";
        return result;
    }

    // 2. 乖離・違和感 (DrasticChange) の判定
    // 普段は温和・ポジティブ（friendlyRatio >= 0.70）なのに、今回ネガティブ発言（mild or strong）が出た場合
    if (friendlyRatio >= 0.70 &&
        (result.currentSentiment == "negative_mild" || result.currentSentiment == "negative_strong" || result.currentTopic == "complaint_venting")) {
        result.status = ObserverStatus::DrasticChange;
        result.statusString = "DrasticChange";
        result.concernLevel = 2;
        result.anomalySummary = "普段は温和・ポジティブな発言が多いが、突然トーンが乖離して不満・強い表現が出現";
        result.directive = "[対話誘導指示: ユーザーから普段の傾向と大きく異なる強い感情・表現が発せられました。否定や説教をせず、『かなり強い言い方だけど、何かあった？』『どうしたの？』のように、ユーザーが自発的に事情を話せるよう優しく聞き返してください。医療的判断は行わず、友人として寄り添う姿勢を維持してください。]";
        return result;
    }

    return result;
}

ObserverEvaluationResult CommunityObserverEngine::recordAndEvaluate(const QString &platform, const QString &user, const QString &text) {
    // 判定を行ってから最新発言を追記保存
    ObserverEvaluationResult eval = evaluateMessage(platform, user, text);
    recordMessage(platform, user, text);
    return eval;
}

int CommunityObserverEngine::vacuumLogs(int maxDays, int maxRecordsPerUser) {
    QDir dir(m_logsDirectory);
    if (!dir.exists()) return 0;

    QStringList filters;
    filters << "*.json";
    QFileInfoList files = dir.entryInfoList(filters, QDir::Files);

    int modifiedCount = 0;
    QDateTime cutoffDate = QDateTime::currentDateTimeUtc().addDays(-maxDays);

    for (const QFileInfo &fi : files) {
        QList<UserMessageRecord> records = loadUserRecords(fi.absoluteFilePath());
        if (records.isEmpty()) continue;

        int originalSize = records.size();

        // 1. 日数による古いレコード削除
        QList<UserMessageRecord> filtered;
        for (const auto &rec : records) {
            if (rec.timestamp >= cutoffDate) {
                filtered.append(rec);
            }
        }

        // 2. 件数制限（最大100件）
        while (filtered.size() > maxRecordsPerUser) {
            filtered.removeFirst();
        }

        if (filtered.size() != originalSize) {
            QString user = fi.baseName();
            QString platform = "twitch";
            int idx = user.indexOf('_');
            if (idx != -1) {
                platform = user.left(idx);
                user = user.mid(idx + 1);
            }
            saveUserRecords(fi.absoluteFilePath(), platform, user, filtered);
            modifiedCount++;
        }
    }

    return modifiedCount;
}

QJsonObject CommunityObserverEngine::inspectUser(const QString &platform, const QString &user) {
    QString filePath = resolveLogFilePath(platform, user);
    QList<UserMessageRecord> records = loadUserRecords(filePath);

    QJsonObject obj;
    obj["user"] = user;
    obj["platform"] = platform;
    obj["total_records"] = records.size();
    obj["log_file"] = filePath;

    if (!records.isEmpty()) {
        obj["first_seen"] = records.first().timestamp.toUTC().toString(Qt::ISODate);
        obj["last_seen"] = records.last().timestamp.toUTC().toString(Qt::ISODate);
    }

    QJsonArray arr;
    for (const auto &r : records) {
        arr.append(r.toJson());
    }
    obj["records"] = arr;
    return obj;
}
