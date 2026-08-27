#!/usr/bin/env python3
"""
AiAssistantAvatar Automated Full Unit Test Runner & Reporter
Runs all Google Test suites (AiAssistantAvatarTest.exe), measures total execution time,
suppresses stdout by default, logs complete raw test output to TestLog/YYYY-MM-DD_HH-MM-SS_TestLog.log,
and writes structured JSON result summary file (build/test_summary.json & test_summary.json).
"""

import os
import sys
import time
import subprocess
import re
import json
from datetime import datetime

# Windows コンソールでの UTF-8 文字出力時の UnicodeEncodeError 回避
if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    except Exception:
        pass

def parse_failed_tests(output_text):
    """
    Google Test の標準出力テキストから失敗したテストケース名（例: SuiteName.TestName）を抽出する
    """
    failed_tests = []
    if not output_text:
        return failed_tests

    # "[  FAILED  ] SuiteName.TestName" のパターンを検索
    pattern = re.compile(r"\[\s*FAILED\s*\]\s+([A-Za-z0-9_]+\.[A-Za-z0-9_]+)")
    for line in output_text.splitlines():
        match = pattern.search(line)
        if match:
            test_name = match.group(1)
            if test_name not in failed_tests:
                failed_tests.append(test_name)
    return failed_tests

def check_trustchain(project_dir):
    """
    Git コマンドを実行し、TrustChain 検証（コミット照合、未コミット差分、ブランチ）を判定する
    """
    res = {
        "verified": False,
        "status": "UNKNOWN",
        "commit": "",
        "branch": "",
        "is_customized": True,
        "details": ""
    }
    try:
        commit_res = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=project_dir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )
        if commit_res.returncode == 0:
            res["commit"] = commit_res.stdout.strip()

        branch_res = subprocess.run(
            ["git", "rev-parse", "--abbrev-ref", "HEAD"],
            cwd=project_dir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )
        if branch_res.returncode == 0:
            res["branch"] = branch_res.stdout.strip()

        status_res = subprocess.run(
            ["git", "status", "--porcelain"],
            cwd=project_dir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )
        is_dirty = bool(status_res.stdout.strip())

        if is_dirty:
            res["status"] = "CUSTOMIZED_DIRTY"
            res["is_customized"] = True
            res["verified"] = False
            res["details"] = "Local uncommitted modifications detected."
            return res

        if res["commit"] and res["branch"]:
            remote_res = subprocess.run(
                ["git", "ls-remote", "origin", res["branch"]],
                cwd=project_dir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
            )
            if remote_res.returncode == 0 and res["commit"] in remote_res.stdout:
                res["status"] = "PASSED"
                res["is_customized"] = False
                res["verified"] = True
                res["details"] = "Commit verified on remote origin."
            else:
                res["status"] = "CUSTOMIZED_UNPUSHED"
                res["is_customized"] = True
                res["verified"] = False
                res["details"] = "Commit not found on remote origin."
    except Exception as e:
        res["status"] = "VERIFICATION_ERROR"
        res["details"] = str(e)

    return res


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = script_dir
    build_dir = os.path.join(project_dir, "build")
    test_logs_dir = os.path.join(project_dir, "TestLog")

    os.makedirs(test_logs_dir, exist_ok=True)
    os.makedirs(build_dir, exist_ok=True)

    # 完了マーカーファイルパス (build/test_summary.json & ルート)
    json_summary_path = os.path.join(build_dir, "test_summary.json")
    json_summary_root = os.path.join(project_dir, "test_summary.json")

    # オプション解析 (--verbose / -v が指定されない限り標準出力は非表示)
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
            if verbose:
                print(f"[Error] Build failed with code {e.returncode}")
            sys.exit(1)

        for candidate in test_exe_candidates:
            if os.path.exists(candidate):
                test_exe = candidate
                break

    if not test_exe or not os.path.exists(test_exe):
        if verbose:
            print(f"[Error] Test executable not found. Tried paths: {test_exe_candidates}")
        sys.exit(1)

    # 4. Qt6 ライブラリパスを PATH に追加
    env = os.environ.copy()
    qt_bin = r"C:\Qt\6.10.1\mingw_64\bin"
    if os.path.exists(qt_bin) and qt_bin not in env.get("PATH", ""):
        env["PATH"] = qt_bin + os.pathsep + env.get("PATH", "")

    # 5. テスト実行とログ収集・所要時間計測
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
            if verbose:
                print(line, end="")
            stdout_lines.append(line)

    process.wait()
    elapsed_seconds = round(time.perf_counter() - start_time, 2)
    output_text = "".join(stdout_lines)

    # 6. テスト結果の解析 ＆ 失敗テスト抽出
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

    failed_test_list = parse_failed_tests(output_text)
    passed_all = (process.returncode == 0 and failed_count == 0 and total_count > 0)
    status_str = "SUCCESS" if passed_all else "FAILURE"
    pass_rate_percent = round((passed_count / total_count * 100.0), 2) if total_count > 0 else 0.0

    # 7. TrustChain 検証の実行
    tc_info = check_trustchain(project_dir)
    tc_status_line = f"TrustChain: {tc_info['status']} (Commit: {tc_info['commit'][:10] if tc_info['commit'] else 'N/A'}, RemoteVerified: {tc_info['verified']}, Details: {tc_info['details']})"

    # 8. テスト出力ログのファイル保存 (TestLog/YYYY-MM-DD_HH-MM-SS_TestLog.log ＆ build/test.log)
    timestamp_str = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    test_log_filename = f"{timestamp_str}_TestLog.log"
    test_log_filepath = os.path.join(test_logs_dir, test_log_filename)
    build_test_log_filepath = os.path.join(build_dir, "test.log")

    log_content = (
        f"============================================================\n"
        f"  AiAssistantAvatar Test Log - {datetime.now().isoformat()}\n"
        f"  Executable: {test_exe}\n"
        f"  {tc_status_line}\n"
        f"  Status: {status_str} | Ran: {total_count} | Passed: {passed_count} | Failed: {failed_count} ({pass_rate_percent}%)\n"
        f"  Elapsed: {elapsed_seconds}s\n"
        f"============================================================\n\n"
        f"{output_text}\n"
    )

    with open(test_log_filepath, "w", encoding="utf-8") as f:
        f.write(log_content)

    with open(build_test_log_filepath, "w", encoding="utf-8") as f:
        f.write(log_content)

    # 9. サマリ JSON ファイルの出力
    summary_data = {
        "timestamp": datetime.now().isoformat(),
        "success": passed_all,
        "status": status_str,
        "total_count": total_count,
        "passed_count": passed_count,
        "failed_count": failed_count,
        "suites_count": suites_count,
        "pass_rate_percent": pass_rate_percent,
        "elapsed_seconds": elapsed_seconds,
        "trustchain": tc_info,
        "failed_tests": failed_test_list,
        "test_log_file": test_log_filepath,
        "test_exe": test_exe
    }

    with open(json_summary_path, "w", encoding="utf-8") as f:
        json.dump(summary_data, f, indent=2, ensure_ascii=False)

    with open(json_summary_root, "w", encoding="utf-8") as f:
        json.dump(summary_data, f, indent=2, ensure_ascii=False)

    if verbose:
        print(f"[Report] Test summary saved to {json_summary_path}")

    sys.exit(0 if passed_all else 1)

if __name__ == "__main__":
    main()
