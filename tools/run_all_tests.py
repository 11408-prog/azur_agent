#!/usr/bin/env python3
"""Azur Agent one-click test runner (pure Python, always uses the project venv).

Runs, in order:
  [1] C++ unit tests      azur_agent_tests.exe  (from a tests-enabled build dir)
  [2] Python unit tests   test_executor / test_memory_cli / test_eval_runner
  [3] AI eval self-check  eval_runner.py --mock (offline)

All Python-side steps are executed with the project's own virtual environment
(azur_agent\\Scripts\\python.exe), so nothing depends on a system Python or an
IDE configuration. The C++ binary is run directly (its protocol tests already
use the venv interpreter internally).

Usage (from anywhere; the project root is located from this file's path):
    azur_agent\\Scripts\\python.exe tools\\run_all_tests.py
    python tools\\run_all_tests.py --build-dir build-test

Exit code = number of failed steps (0 = all passed).
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VENV_PY = os.path.join(REPO_ROOT, "azur_agent", "Scripts", "python.exe")

# Machine-specific paths, kept in one place so they are easy to adjust.
DEFAULT_QT_BIN = r"C:\Qt\6.11.1\mingw_64\bin"
DEFAULT_CMAKE = r"C:\Qt\Tools\CMake_64\bin\cmake.exe"
DEFAULT_QT_PREFIX = "C:/Qt/6.11.1/mingw_64"
DEFAULT_CXX = r"C:\Qt\Tools\mingw1310_64\bin\g++.exe"


def sh(cmd, cwd=None, env=None) -> int:
    print("$ " + " ".join(str(c) for c in cmd))
    return subprocess.call(cmd, cwd=cwd, env=env)


def pause_if_double_clicked() -> None:
    """On Windows, double-clicking a .py closes the console instantly on exit.

    Detect a console that was created just for this process (exactly one process
    attached) and wait for Enter so the results can be read. When run from an
    existing terminal there are >= 2 processes attached; when run through a
    non-console shell (Git Bash / CI / ssh) there are 0. Both skip the pause,
    so only a real double-click ever blocks.
    """
    if sys.platform != "win32":
        return
    try:
        import ctypes
        buf = (ctypes.c_uint * 2)()
        n = ctypes.windll.kernel32.GetConsoleProcessList(buf, 2)
        if n == 1:
            input("\nPress Enter to close...")
    except Exception:
        pass


def run_venv(args, env=None) -> int:
    """Run a Python script with the project venv interpreter."""
    return sh([VENV_PY] + args, env=env)


def ensure_cpp_tests(build_dir: str, env) -> None:
    """If azur_agent_tests.exe is missing, configure + build it once."""
    cmake = os.environ.get("AZUR_CMAKE", DEFAULT_CMAKE)
    rc = sh([cmake, "-B", build_dir, "-G", "MinGW Makefiles",
             "-DCMAKE_PREFIX_PATH=" + os.environ.get("AZUR_QT_PREFIX", DEFAULT_QT_PREFIX),
             "-DCMAKE_CXX_COMPILER=" + os.environ.get("AZUR_CXX", DEFAULT_CXX),
             "-DAZUR_BUILD_TESTS=ON"], env=env)
    if rc != 0:
        return
    sh([cmake, "--build", build_dir, "--target", "azur_agent_tests"], env=env)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description="Azur Agent one-click tests (C++ + Python + AI eval) via the project venv.")
    parser.add_argument("--build-dir",
                        default=os.path.join(REPO_ROOT, "build-test"),
                        help="tests-enabled build dir containing azur_agent_tests.exe")
    parser.add_argument("--qt-bin", default=os.environ.get("AZUR_QT_BIN", DEFAULT_QT_BIN),
                        help="Qt bin dir to put on PATH for the C++ test binary")
    args = parser.parse_args(argv)

    if not os.path.exists(VENV_PY):
        print(f"[ERROR] venv interpreter not found: {VENV_PY}")
        print("        create it first: python -m venv azur_agent")
        return 1

    # Qt DLLs must be on PATH for the C++ test binary at runtime (Windows).
    env = dict(os.environ)
    if sys.platform == "win32" and os.path.isdir(args.qt_bin):
        env["PATH"] = args.qt_bin + os.pathsep + env.get("PATH", "")

    failed = 0

    print("=" * 60)
    print("[1/3] C++ unit tests")
    print(f"      {args.build_dir}")
    print("=" * 60)
    test_exe = os.path.join(args.build_dir, "azur_agent_tests.exe")
    if not os.path.exists(test_exe):
        print("[INFO] test binary not found; configuring + building once (needs network for GoogleTest)...")
        ensure_cpp_tests(args.build_dir, env)
    if not os.path.exists(test_exe):
        print(f"[FAIL] still no {test_exe}; C++ tests skipped.")
        failed += 1
    else:
        rc = sh([test_exe], env=env)
        if rc == 0:
            print("[PASS] C++ unit tests OK.")
        else:
            print("[FAIL] C++ unit tests reported failures.")
            failed += 1
    print()

    print("=" * 60)
    print("[2/3] Python unit tests  (venv: " + VENV_PY + ")")
    print("=" * 60)
    test_files = [
        "test_executor.py",
        "test_memory_cli.py",
        "test_eval_runner.py",
    ]
    for name in test_files:
        path = os.path.join(REPO_ROOT, "python", "azur_tools", "tests", name)
        rc = run_venv([path], env=env)
        if rc == 0:
            print(f"[PASS] {name} OK.")
        else:
            print(f"[FAIL] {name} reported failures.")
            failed += 1
        print()
    print()

    print("=" * 60)
    print("[3/3] AI eval self-check  (mock, offline)")
    print("=" * 60)
    rc = run_venv([os.path.join(REPO_ROOT, "python", "azur_tools", "eval", "eval_runner.py"),
                   "--mock"], env=env)
    if rc == 0:
        print("[PASS] AI eval self-check OK.")
    else:
        print("[FAIL] AI eval self-check reported failures.")
        failed += 1
    print()

    print("=" * 60)
    if failed == 0:
        print("ALL TESTS PASSED.")
    else:
        print(f"FAILED STEPS: {failed}")
    return failed


if __name__ == "__main__":
    code = main()
    pause_if_double_clicked()
    sys.exit(code)
