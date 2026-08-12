#ifndef CONFIG_UTILS_H
#define CONFIG_UTILS_H

#include <QString>
#include <QStringList>
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QDebug>

namespace ConfigUtils {
    /**
     * @brief Config/ ディレクトリ配下の設定ファイルパスを一元解決する。
     * 1. QCoreApplication::applicationDirPath() + "/Config/" + fileName
     * 2. "Config/" + fileName
     * 3. PROJECT_SOURCE_DIR + "/Config/" + fileName (デバッグビルド時)
     *
     * 存在しない場合、Config/ 配下の .sample ファイルから自動複製生成を行う。
     */
    inline QString resolveConfigFilePath(const QString &fileName) {
        QString appDir = QCoreApplication::applicationDirPath();
        QStringList candidates = {
            appDir + "/Config/" + fileName,
            "Config/" + fileName
        };

#ifdef PROJECT_SOURCE_DIR
        candidates.append(QString(PROJECT_SOURCE_DIR) + "/Config/" + fileName);
#endif

        for (const QString &path : candidates) {
            if (QFile::exists(path)) {
                return QDir::cleanPath(path);
            }
        }

        // Config/ 配下にファイルが存在しない場合、.sample から自動複製
        QString defaultTarget = appDir + "/Config/" + fileName;
        QString sampleName = fileName + ".sample";
        QStringList sampleCandidates = {
            appDir + "/Config/" + sampleName,
            "Config/" + sampleName
        };

#ifdef PROJECT_SOURCE_DIR
        sampleCandidates.append(QString(PROJECT_SOURCE_DIR) + "/Config/" + sampleName);
#endif

        for (const QString &samplePath : sampleCandidates) {
            if (QFile::exists(samplePath)) {
                QFileInfo targetInfo(defaultTarget);
                QDir().mkpath(targetInfo.absolutePath());
                if (QFile::copy(samplePath, defaultTarget)) {
                    QFile::setPermissions(defaultTarget, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadUser | QFileDevice::WriteUser);
                    qDebug() << "ConfigUtils: Auto-created" << defaultTarget << "from" << samplePath;
                    return QDir::cleanPath(defaultTarget);
                }
            }
        }

        return QDir::cleanPath(defaultTarget);
    }
}

#endif // CONFIG_UTILS_H
