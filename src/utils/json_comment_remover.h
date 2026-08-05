#ifndef JSON_COMMENT_REMOVER_H
#define JSON_COMMENT_REMOVER_H

#include <QByteArray>
#include <QString>

class JsonCommentRemover {
public:
    static QByteArray stripHashComments(const QByteArray &jsonBytes);
    static QString stripHashComments(const QString &jsonText);
};

#endif // JSON_COMMENT_REMOVER_H
