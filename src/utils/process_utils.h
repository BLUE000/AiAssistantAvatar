#pragma once

#include <QString>
#include <QProcess>

class ProcessUtils {
public:
    /**
     * @brief 実行ファイル名を元に、実行可能な絶対パスまたは相対パスを解決します。
     * 探索優先順位:
     * 1. <appDir>/tools/<appName>
     * 2. <appDir>/<appName>
     * 3. <appDir>/build/<appName>
     * 4. <currentDir>/build/<appName>
     * 5. <currentDir>/tools/<appName>
     * 6. <currentDir>/<appName>
     * 7. <appName>
     */
    static QString resolveExecutablePath(const QString &appName);

    /**
     * @brief QProcess に対し、ルートディレクトリ（appDir）を PATH の先頭に追加した環境変数を設定します。
     * これにより、tools/ サブディレクトリ内の EXE がルート階層の Qt6 / MinGW DLL 群を自動ロードできます。
     */
    static void configureProcessEnvironment(QProcess &process);
};
