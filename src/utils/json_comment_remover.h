#ifndef JSON_COMMENT_REMOVER_H
#define JSON_COMMENT_REMOVER_H

#include <QByteArray>
#include <QString>
#include <QJsonObject>


class JsonCommentRemover {
public:
    static QByteArray stripHashComments(const QByteArray &jsonBytes);
    static QString stripHashComments(const QString &jsonText);

    // 既存 JSON テキストのコメント行・フォーマットを保護しつつ、newObj の値でピンポイント更新する
    static QString updateExistingJsonText(const QString &existingText, const QJsonObject &newObj);

};

#endif // JSON_COMMENT_REMOVER_H
