#!/usr/bin/env python3
"""
AiAssistantAvatar Automated Build Runner & Reporter
Executes CMake Release build, measures compilation time, keeps console output minimal for fast execution,
and writes a JSON build summary file (build/build_summary.json) for automated monitoring.
"""

import os
import sys
import time
import subprocess
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
    
    # 完了判定マーカーファイルパス (build/build_summary.json & ルート)
    json_summary_path = os.path.join(build_dir, "build_summary.json")
    json_summary_root = os.path.join(project_dir, "build_summary.json")

    verbose = "--verbose" in sys.argv or "-v" in sys.argv
    clean = "--clean" in sys.argv

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

    print("============================================================")
    print("         Starting AiAssistantAvatar CMake Build            ")
    print("============================================================")

    start_time = time.perf_counter()
    success = False
    error_message = ""

    try:
        # CMake Configure
        print("[1/2] Configuring CMake (Release)...", end="", flush=True)
        config_cmd = ["cmake", "-S", project_dir, "-B", build_dir, "-DCMAKE_BUILD_TYPE=Release"]
        p_config = subprocess.run(
            config_cmd,
            stdout=subprocess.PIPE if not verbose else None,
            stderr=subprocess.PIPE if not verbose else None,
            text=True,
            encoding="utf-8",
            errors="replace",
            env=env,
            cwd=project_dir
        )
        if p_config.returncode != 0:
            print(" [FAILED]")
            error_message = p_config.stderr or "CMake configure failed."
            if not verbose:
                print(error_message)
        else:
            print(" [OK]")

            # CMake Build
            print("[2/2] Building targets (Release)...", end="", flush=True)
            build_cmd = ["cmake", "--build", build_dir, "--config", "Release"]
            if clean:
                build_cmd.append("--clean-first")

            p_build = subprocess.run(
                build_cmd,
                stdout=subprocess.PIPE if not verbose else None,
                stderr=subprocess.PIPE if not verbose else None,
                text=True,
                encoding="utf-8",
                errors="replace",
                env=env,
                cwd=project_dir
            )
            if p_build.returncode != 0:
                print(" [FAILED]")
                error_message = p_build.stderr or "CMake build failed."
                if not verbose:
                    print(error_message)
            else:
                print(" [OK]")
                success = True

    except Exception as e:
        error_message = str(e)
        print(f"\n[Error] Build process exception: {e}")

    elapsed_seconds = round(time.perf_counter() - start_time, 2)

    # 時間のフォーマット
    if elapsed_seconds >= 60:
        minutes = int(elapsed_seconds // 60)
        seconds = elapsed_seconds % 60
        time_str = f"{minutes}m {seconds:.2f}s ({elapsed_seconds:.2f}s)"
    else:
        time_str = f"{elapsed_seconds:.2f} seconds"

    status_str = "SUCCESS" if success else "FAILURE"

    print("\n============================================================")
    print("         AiAssistantAvatar Build Summary                    ")
    print("============================================================")
    print(f" Build Type     : Release")
    print(f" Execution Time : {time_str}")
    print(f" Result Status  : [ {status_str} ] {'Build completed cleanly!' if success else 'Build failed!'}")
    print("============================================================\n")

    # 3. 完了判定用サマリ JSON ファイルの書き出し
    summary_data = {
        "timestamp": datetime.now().isoformat(),
        "success": success,
        "status": status_str,
        "build_type": "Release",
        "elapsed_seconds": elapsed_seconds,
        "error_message": error_message
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
