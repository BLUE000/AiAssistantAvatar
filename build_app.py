#!/usr/bin/env python3
"""
AiAssistantAvatar Automated Build Runner & Reporter
Executes CMake Release build, measures compilation time, captures compiler/CMake warnings,
keeps console output minimal for fast execution, and writes a JSON build summary file
(build/build_summary.json) for automated monitoring.
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

def parse_warnings(output_text):
    """ビルド出力ログからコンパイラおよび CMake のワーニング行を抽出しリスト化"""
    warnings = []
    if not output_text:
        return warnings

    warning_pattern = re.compile(r"(warning[:\s]|CMake Warning|warning C\d+|:\s*warning\s*:)", re.IGNORECASE)

    for line in output_text.splitlines():
        line_str = line.strip()
        if warning_pattern.search(line_str):
            # デバッグログ等の紛らわしい文字列を除外
            if "Debug: " in line_str or "Info: " in line_str:
                continue
            if line_str not in warnings:
                warnings.append(line_str)

    return warnings

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = script_dir
    build_dir = os.path.join(project_dir, "build")
    build_logs_dir = os.path.join(project_dir, "BuildLog")
    
    # 完了判定マーカーファイルパス (build/build_summary.json & ルート)
    json_summary_path = os.path.join(build_dir, "build_summary.json")
    json_summary_root = os.path.join(project_dir, "build_summary.json")

    verbose = "--verbose" in sys.argv or "-v" in sys.argv
    quiet = "--quiet" in sys.argv or "-q" in sys.argv
    # リリースビルド時はデフォルトでクリーンビルドを実行 (--no-clean 指定時のみ無効化)
    clean = "--no-clean" not in sys.argv

    # 1. 以前の完了判定マーカーファイルを事前に削除
    for path in [json_summary_path, json_summary_root]:
        if os.path.exists(path):
            try:
                os.remove(path)
            except OSError:
                pass

    # 2. Qt6 ライブラリパスを PATH に追加
    env = os.environ.copy()
    qt_bin = r"C:\Qt\6.10.1\mingw_64\bin"
    if os.path.exists(qt_bin) and qt_bin not in env.get("PATH", ""):
        env["PATH"] = qt_bin + os.pathsep + env.get("PATH", "")

    if not quiet:
        print("============================================================")
        print("         Starting AiAssistantAvatar CMake Build            ")
        print("============================================================")

    start_time = time.perf_counter()
    start_timestamp = datetime.now()
    success = False
    error_message = ""
    captured_logs = []

    try:
        # 1. CMake Configure
        if verbose:
            print("[1/2] Configuring CMake (Release)...", end="", flush=True)
        config_cmd = ["cmake", "-S", project_dir, "-B", build_dir, "-DCMAKE_BUILD_TYPE=Release"]
        p_config = subprocess.run(
            config_cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            env=env,
            cwd=project_dir
        )
        captured_logs.append("=== CMake Configure Log ===")
        captured_logs.append(p_config.stdout or "")

        if verbose and p_config.stdout:
            print("\n" + p_config.stdout)

        if p_config.returncode != 0:
            if verbose:
                print(" [FAILED]")
            error_message = p_config.stdout or "CMake configure failed."
            if not quiet and not verbose:
                print(error_message)
        else:
            if verbose:
                print(" [OK]")

            # 2. CMake Build
            if verbose:
                print("[2/2] Building targets (Release)...", end="", flush=True)
            build_cmd = ["cmake", "--build", build_dir, "--config", "Release"]
            if clean:
                build_cmd.append("--clean-first")

            p_build = subprocess.run(
                build_cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding="utf-8",
                errors="replace",
                env=env,
                cwd=project_dir
            )
            captured_logs.append("=== CMake Build Log ===")
            captured_logs.append(p_build.stdout or "")

            if verbose and p_build.stdout:
                print("\n" + p_build.stdout)

            if p_build.returncode != 0:
                if verbose:
                    print(" [FAILED]")
                error_message = p_build.stdout or "CMake build failed."
                if not quiet and not verbose:
                    print(error_message)
            else:
                if verbose:
                    print(" [OK]")
                success = True

    except Exception as e:
        error_message = str(e)
        if not quiet:
            print(f"\n[Error] Build process exception: {e}")

    elapsed_seconds = round(time.perf_counter() - start_time, 2)
    all_output = "\n".join(captured_logs)
    detected_warnings = parse_warnings(all_output)
    warning_count = len(detected_warnings)

    # ビルドログ全体をフォルダ (log/build_logs/) へタイムスタンプ蓄積保存 (Git管理外)
    os.makedirs(build_logs_dir, exist_ok=True)
    timestamp_str = start_timestamp.strftime("%Y-%m-%d_%H-%M-%S")
    build_log_file = os.path.join(build_logs_dir, f"{timestamp_str}_BuildLog.log")
    latest_build_log = os.path.join(build_dir, "build.log")

    try:
        with open(build_log_file, "w", encoding="utf-8") as f:
            f.write(all_output)
        with open(latest_build_log, "w", encoding="utf-8") as f:
            f.write(all_output)
    except Exception as log_err:
        if not quiet:
            print(f"[Warning] Failed to write build log file: {log_err}")

    # 時間のフォーマット
    if elapsed_seconds >= 60:
        minutes = int(elapsed_seconds // 60)
        seconds = elapsed_seconds % 60
        time_str = f"{minutes}m {seconds:.2f}s ({elapsed_seconds:.2f}s)"
    else:
        time_str = f"{elapsed_seconds:.2f} seconds"

    status_str = "SUCCESS" if success else "FAILURE"

    if not quiet:
        print("\n============================================================")
        print("         AiAssistantAvatar Build Summary                    ")
        print("============================================================")
        print(f" Build Type     : Release (Clean build: {'Yes' if clean else 'No'})")
        print(f" Execution Time : {time_str}")
        print(f" Warning Count  : {warning_count} {'(No warnings)' if warning_count == 0 else 'warning(s) detected!'}")
        if warning_count > 0:
            print(" Warning Details:")
            for w in detected_warnings[:10]: # 最大10件表示
                print(f"   - {w}")
            if warning_count > 10:
                print(f"   ... and {warning_count - 10} more warnings.")
        print(f" Result Status  : [ {status_str} ] {'Build completed cleanly!' if success else 'Build failed!'}")
        print(f" Saved Log File : {build_log_file}")
        print("============================================================\n")

    # 3. 完了判定用サマリ JSON ファイルの書き出し
    summary_data = {
        "timestamp": start_timestamp.isoformat(),
        "success": success,
        "status": status_str,
        "build_type": "Release",
        "clean_build": clean,
        "elapsed_seconds": elapsed_seconds,
        "warning_count": warning_count,
        "warnings": detected_warnings,
        "error_message": error_message,
        "build_log_file": build_log_file
    }

    os.makedirs(build_dir, exist_ok=True)
    with open(json_summary_path, "w", encoding="utf-8") as f:
        json.dump(summary_data, f, indent=2, ensure_ascii=False)

    with open(json_summary_root, "w", encoding="utf-8") as f:
        json.dump(summary_data, f, indent=2, ensure_ascii=False)

    print(f"[Report] Build summary saved to {json_summary_path}")

    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
