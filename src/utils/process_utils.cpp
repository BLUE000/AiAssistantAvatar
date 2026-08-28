#include "process_utils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcessEnvironment>

QString ProcessUtils::resolveExecutablePath(const QString &appName) {
    QString targetName = appName;
#ifdef Q_OS_WIN
    if (!targetName.endsWith(".exe", Qt::CaseInsensitive)) {
        targetName += ".exe";
    }
#endif

    QString appDir = QCoreApplication::applicationDirPath();
    QString currentDir = QDir::currentPath();

    QStringList candidates = {
        appDir + "/tools/" + targetName,
        appDir + "/" + targetName,
        appDir + "/build/" + targetName,
        currentDir + "/build/" + targetName,
        currentDir + "/tools/" + targetName,
        currentDir + "/" + targetName,
        targetName
    };

    for (const QString &path : candidates) {
        if (QFile::exists(path)) {
            return path;
        }
    }
    return candidates.first();
}

void ProcessUtils::configureProcessEnvironment(QProcess &process) {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString appDir = QCoreApplication::applicationDirPath();
    QString currentPath = env.value("PATH");
    env.insert("PATH", appDir + ";" + appDir + "/tools;" + currentPath);
    env.insert("QT_PLUGIN_PATH", appDir + ";" + appDir + "/plugins;" + appDir + "/tools");
    process.setProcessEnvironment(env);
}
