#!/usr/bin/env python3
"""
AiAssistantAvatar Automated Full Unit Test Runner & Reporter
Runs all Google Test suites (AiAssistantAvatarTest.exe), measures total execution time,
keeps console output minimal for fast execution, and writes JSON result summary file
(build/test_summary.json) which acts as a completion marker for automated background task monitoring.
"""

import os
import sys
import time
import subprocess
import re
import json
from datetime import datetime

# Windows コンソールでの UTF-8 文字出力（\ufffd 等）時の UnicodeEncodeError 回避
if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    except Exception:
        pass

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = script_dir
    build_dir = os.path.join(project_dir, "build")
    
    # 完了マーカーファイルパス (build/test_summary.json & ルート)
    json_summary_path = os.path.join(build_dir, "test_summary.json")
    json_summary_root = os.path.join(project_dir, "test_summary.json")

    # オプション解析 (--verbose が指定されない限り標準出力は最低限に抑えて高速化)
    verbose = "--verbose" in sys.argv or "-v" in sys.argv
    do_build = "--build" in sys.argv

    # 1. 以前の完了判定マーカーファイルを事前に削除
    for path in [json_summary_path, json_summary_root]:
        if os.path.exists(path):
            try:
                os.remove(path)
            except OSError:
                pass

    # 2. ターゲット実行ファイルの探索 (build 直下 または Release)
    test_exe_candidates = [
        os.path.join(build_dir, "AiAssistantAvatarTest.exe"),
        os.path.join(build_dir, "Release", "AiAssistantAvatarTest.exe")
    ]
    
    test_exe = None
    for candidate in test_exe_candidates:
        if os.path.exists(candidate):
            test_exe = candidate
            break

    # 3. ビルド未実施または --build 指定時、自動ビルドを試行
    if not test_exe or do_build:
        if verbose:
            print("[Build] Executing CMake build for AiAssistantAvatarTest...")
        try:
            subprocess.run(
                ["cmake", "-S", project_dir, "-B", build_dir, "-DCMAKE_BUILD_TYPE=Release"],
                stdout=subprocess.PIPE if not verbose else None,
                stderr=subprocess.PIPE if not verbose else None,
                check=True
            )
            subprocess.run(
                ["cmake", "--build", build_dir, "--config", "Release"],
                stdout=subprocess.PIPE if not verbose else None,
                stderr=subprocess.PIPE if not verbose else None,
                check=True
            )
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

    # 4. Qt6 ライブラリパスを PATH に追加
    env = os.environ.copy()
    qt_bin = r"C:\Qt\6.10.1\mingw_64\bin"
    if os.path.exists(qt_bin) and qt_bin not in env.get("PATH", ""):
        env["PATH"] = qt_bin + os.pathsep + env.get("PATH", "")

    print("============================================================")
    print("      Starting AiAssistantAvatar Full Unit Test Suite       ")
    print(f" Executable: {test_exe}")
    print("============================================================\n")

    # 5. テスト実行と所要時間の計測
    start_time = time.perf_counter()
    print("Running Google Test suites...", end="", flush=True)

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
            if verbose:
                print(line, end="")
            stdout_lines.append(line)

    process.wait()
    elapsed_seconds = round(time.perf_counter() - start_time, 2)
    print(" [DONE]")

    # 6. テスト結果の解析
    output_text = "".join(stdout_lines)
    
    passed_count = 0
    failed_count = 0
    total_count = 0
    suites_count = 0

    match_total = re.search(r"\[==========\] (\d+) tests from (\d+) test suites ran", output_text)
    if match_total:
        total_count = int(match_total.group(1))
        suites_count = int(match_total.group(2))

    match_passed = re.search(r"\[  PASSED  \] (\d+) tests\.", output_text)
    if match_passed:
        passed_count = int(match_passed.group(1))

    match_failed = re.search(r"\[  FAILED  \] (\d+) tests", output_text)
    if match_failed:
        failed_count = int(match_failed.group(1))

    passed_all = (process.returncode == 0 and failed_count == 0 and total_count > 0)
    status_str = "SUCCESS" if passed_all else "FAILURE"

    # 時間のフォーマット
    if elapsed_seconds >= 60:
        minutes = int(elapsed_seconds // 60)
        seconds = elapsed_seconds % 60
        time_str = f"{minutes}m {seconds:.2f}s ({elapsed_seconds:.2f}s)"
    else:
        time_str = f"{elapsed_seconds:.2f} seconds"

    # 7. まとめ結果のコンソール表示
    print("\n============================================================")
    print("         AiAssistantAvatar Unit Test Execution Summary      ")
    print("============================================================")
    print(f" Total Test Suites : {suites_count}")
    print(f" Total Tests Ran   : {total_count}")
    print(f" Passed Tests      : {passed_count}")
    print(f" Failed Tests      : {failed_count}")
    print(f" Execution Time    : {time_str}")
    print(f" Result Status     : [ {status_str} ] {'All tests passed cleanly!' if passed_all else f'{failed_count} test(s) failed!'}")
    print("============================================================\n")

    # 8. 完了判定用サマリ JSON ファイルの書き出し
    summary_data = {
        "timestamp": datetime.now().isoformat(),
        "passed_all": passed_all,
        "status": status_str,
        "total_count": total_count,
        "passed_count": passed_count,
        "failed_count": failed_count,
        "suites_count": suites_count,
        "elapsed_seconds": elapsed_seconds,
        "test_exe": test_exe
    }

    os.makedirs(build_dir, exist_ok=True)
    with open(json_summary_path, "w", encoding="utf-8") as f:
        json.dump(summary_data, f, indent=2, ensure_ascii=False)

    with open(json_summary_root, "w", encoding="utf-8") as f:
        json.dump(summary_data, f, indent=2, ensure_ascii=False)

    print(f"[Report] Test summary saved to {json_summary_path}")

    sys.exit(0 if passed_all else 1)

if __name__ == "__main__":
    main()
