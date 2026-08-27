#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QProcess>
#include "../src/utils/process_utils.h"

class ProcessUtilsTest : public ::testing::Test {
protected:
    void SetUp() override {
    }

    void TearDown() override {
    }
};

// UT-TOOLS-PATH-01: tools/ サブフォルダ優先探索
TEST_F(ProcessUtilsTest, ResolveExecutablePath_ToolsSubdirPriority) {
    QString appDir = QCoreApplication::applicationDirPath();
    QDir dir(appDir);

    // 一時ディレクトリを作成して検証
    QString toolsDir = appDir + "/tools";
    dir.mkpath(toolsDir);

    QString testExeTools = toolsDir + "/TestDummyTool.exe";
    QString testExeRoot = appDir + "/TestDummyTool.exe";

    QFile f1(testExeTools);
    f1.open(QIODevice::WriteOnly);
    f1.close();

    QFile f2(testExeRoot);
    f2.open(QIODevice::WriteOnly);
    f2.close();

    QString resolved = ProcessUtils::resolveExecutablePath("TestDummyTool");
    EXPECT_TRUE(resolved.contains("/tools/TestDummyTool.exe") || resolved.contains("\\tools\\TestDummyTool.exe"));

    f1.remove();
    f2.remove();
}

// UT-TOOLS-PATH-02: 同一フォルダフォールバック探索
TEST_F(ProcessUtilsTest, ResolveExecutablePath_RootFallback) {
    QString appDir = QCoreApplication::applicationDirPath();
    QString testExeRoot = appDir + "/TestDummyRootOnly.exe";

    QFile f(testExeRoot);
    f.open(QIODevice::WriteOnly);
    f.close();

    QString resolved = ProcessUtils::resolveExecutablePath("TestDummyRootOnly");
    EXPECT_TRUE(resolved.endsWith("TestDummyRootOnly.exe"));
    EXPECT_FALSE(resolved.contains("/tools/") || resolved.contains("\\tools\\"));

    f.remove();
}

// UT-TOOLS-PATH-03: 開発環境 build/ フォルダ探索
TEST_F(ProcessUtilsTest, ResolveExecutablePath_BuildDirFallback) {
    QString appDir = QCoreApplication::applicationDirPath();
    QDir dir(appDir);
    QString buildDir = appDir + "/build";
    dir.mkpath(buildDir);

    QString testExeBuild = buildDir + "/TestDummyBuildOnly.exe";
    QFile f(testExeBuild);
    f.open(QIODevice::WriteOnly);
    f.close();

    QString resolved = ProcessUtils::resolveExecutablePath("TestDummyBuildOnly");
    EXPECT_TRUE(resolved.contains("TestDummyBuildOnly.exe"));

    f.remove();
}

// UT-TOOLS-PATH-04: プロセス環境変数 PATH への appDir 前置注入
TEST_F(ProcessUtilsTest, ConfigureProcessEnvironment_InjectsPath) {
    QProcess process;
    ProcessUtils::configureProcessEnvironment(process);

    QString appDir = QCoreApplication::applicationDirPath();
    QString pathValue = process.processEnvironment().value("PATH");

    EXPECT_TRUE(pathValue.startsWith(appDir));
}
