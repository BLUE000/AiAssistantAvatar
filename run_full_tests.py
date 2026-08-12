#!/usr/bin/env python3
"""
AiAssistantAvatar Automated Full Unit Test Runner
Runs all Google Test suites (AiAssistantAvatarTest.exe), measures total execution time,
and outputs a clean, comprehensive summary of test results and timing metrics.
"""

import os
import sys
import time
import subprocess
import re

# Windows コンソールでの UTF-8 文字出力（\ufffd 等）時の UnicodeEncodeError 回避
if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    except Exception:
        pass

def main():
    # 1. プロジェクトルートと環境変数の設定
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = script_dir
    build_dir = os.path.join(project_dir, "build")
    
    # ターゲット実行ファイルの探索 (build 直下 または Release)
    test_exe_candidates = [
        os.path.join(build_dir, "AiAssistantAvatarTest.exe"),
        os.path.join(build_dir, "Release", "AiAssistantAvatarTest.exe")
    ]
    
    test_exe = None
    for candidate in test_exe_candidates:
        if os.path.exists(candidate):
            test_exe = candidate
            break

    # 2. ビルド未実施の場合、自動ビルドを試行
    if not test_exe or "--build" in sys.argv:
        print("[Build] Executing CMake build for AiAssistantAvatarTest...")
        try:
            subprocess.run(["cmake", "-S", project_dir, "-B", build_dir, "-DCMAKE_BUILD_TYPE=Release"], check=True)
            subprocess.run(["cmake", "--build", build_dir, "--config", "Release"], check=True)
        except subprocess.CalledProcessError as e:
            print(f"[Error] Build failed with code {e.returncode}")
            sys.exit(1)

        for candidate in test_exe_candidates:
            if os.path.exists(candidate):
                test_exe = candidate
                break

    if not test_exe or not os.path.exists(test_exe):
        print(f"[Error] Test executable not found. Tried paths: {test_exe_candidates}")
        sys.exit(1)

    # 3. Qt6 ライブラリパスを PATH に追加
    env = os.environ.copy()
    qt_bin = r"C:\Qt\6.10.1\mingw_64\bin"
    if os.path.exists(qt_bin) and qt_bin not in env.get("PATH", ""):
        env["PATH"] = qt_bin + os.pathsep + env.get("PATH", "")

    print("============================================================")
    print("      Starting AiAssistantAvatar Full Unit Test Suite       ")
    print(f" Executable: {test_exe}")
    print("============================================================\n")

    # 4. テスト実行と所要時間の計測
    start_time = time.perf_counter()
    
    process = subprocess.Popen(
        [test_exe],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        env=env,
        cwd=project_dir
    )

    stdout_lines = []
    while True:
        line = process.stdout.readline()
        if not line and process.poll() is not None:
            break
        if line:
            print(line, end="")
            stdout_lines.append(line)

    process.wait()
    elapsed_seconds = time.perf_counter() - start_time

    # 5. テスト結果の解析
    output_text = "".join(stdout_lines)
    
    passed_count = 0
    failed_count = 0
    total_count = 0
    suites_count = 0

    # 例: [==========] 110 tests from 23 test suites ran.
    match_total = re.search(r"\[==========\] (\d+) tests from (\d+) test suites ran", output_text)
    if match_total:
        total_count = int(match_total.group(1))
        suites_count = int(match_total.group(2))

    # 例: [  PASSED  ] 110 tests.
    match_passed = re.search(r"\[  PASSED  \] (\d+) tests\.", output_text)
    if match_passed:
        passed_count = int(match_passed.group(1))

    # 例: [  FAILED  ] 12 tests, listed below:
    match_failed = re.search(r"\[  FAILED  \] (\d+) tests", output_text)
    if match_failed:
        failed_count = int(match_failed.group(1))

    # 時間の読みやすいフォーマット化
    if elapsed_seconds >= 60:
        minutes = int(elapsed_seconds // 60)
        seconds = elapsed_seconds % 60
        time_str = f"{minutes}m {seconds:.2f}s ({elapsed_seconds:.2f}s)"
    else:
        time_str = f"{elapsed_seconds:.2f} seconds"

    # 6. まとめ結果の表示
    print("\n============================================================")
    print("         AiAssistantAvatar Unit Test Execution Summary      ")
    print("============================================================")
    print(f" Total Test Suites : {suites_count}")
    print(f" Total Tests Ran   : {total_count}")
    print(f" Passed Tests      : {passed_count}")
    print(f" Failed Tests      : {failed_count}")
    print(f" Execution Time    : {time_str}")
    
    if process.returncode == 0 and failed_count == 0 and total_count > 0:
        print(" Success Rate      : 100% PASS")
        print(" Result Status     : [ SUCCESS ] All tests passed cleanly!")
        print("============================================================\n")
        sys.exit(0)
    else:
        print(f" Success Rate      : {(passed_count / max(total_count, 1)) * 100:.1f}%")
        print(f" Result Status     : [ FAILURE ] {failed_count} test(s) failed!")
        print("============================================================\n")
        sys.exit(1)

if __name__ == "__main__":
    main()
