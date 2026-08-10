"""演示 / 测试程序：在 IDE 或终端里直接运行查看效果。

用法（先激活虚拟环境 azur_agent）：
    source azur_agent/Scripts/activate      # Windows 下
    python python/azur_tools/demo.py

会创建一个临时工作区，依次执行若干工具调用场景并打印结果：
    - 打印工具 JSON Schema
    - 列目录（根目录）
    - 读取文本文件
    - 读取不存在的文件
    - 误用 read_file 读目录
    - 读取二进制文件（被拒绝）
    - 路径穿越（../ 越权，被拒绝）
    - 不存在的工具
    - 白名单外路径被拒绝 / 白名单内路径放行
    - CLI 协议（通过子进程走一遍 stdin/stdout JSON 管道）
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile

# 让脚本在"直接运行"方式下也能找到 azur_tools 包（包在 python/ 目录下）
_PKG_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _PKG_DIR not in sys.path:
    sys.path.insert(0, _PKG_DIR)

from azur_tools.executor import execute, tool_definitions  # noqa: E402


def build_workspace(root: str) -> None:
    os.makedirs(os.path.join(root, "src"), exist_ok=True)
    os.makedirs(os.path.join(root, "data"), exist_ok=True)
    os.makedirs(os.path.join(root, "assets"), exist_ok=True)

    with open(os.path.join(root, "src", "main.py"), "w", encoding="utf-8") as f:
        f.write("# main.py\n\ndef hello():\n    print('hi')\n")

    with open(os.path.join(root, "data", "notes.txt"), "w", encoding="utf-8") as f:
        f.write("会议纪要：\n- 明天发布\n- 准备演示\n")

    with open(os.path.join(root, "assets", "blob.bin"), "wb") as f:
        f.write(b"\x00\x01\x02\x00BINARY")


def run_scenario(title: str, workspace: str, tool_name: str, args: dict,
                 allowed: list[str] | None = None,
                 expect_ok: bool | None = None) -> bool:
    """执行一个场景。expect_ok: True=预期成功, False=预期拒绝/错误, None=不校验。"""
    content, ok, label = execute(workspace, tool_name, args, allowed or [])
    print(f"\n=== {title} ===")
    print(f"工具: {tool_name} | 参数: {json.dumps(args, ensure_ascii=False)}")
    print(f"ok: {ok} | 步骤指示: {label}")
    shown = content if len(content) <= 300 else content[:300] + " ...(截断)"
    print(f"content:\n{shown}")

    if expect_ok is None:
        return True
    matched = (ok == expect_ok)
    expect_text = "成功" if expect_ok else "拒绝/错误"
    mark = "符合预期" if matched else "!! 不符合预期 !!"
    print(f"预期: {expect_text} | {mark}")
    return matched


def cli_pipe_test(workspace: str, cli_path: str) -> bool:
    """通过子进程跑一遍 cli.py 的 stdin/stdout JSON 协议。"""
    print(f"\n=== CLI 协议（子进程 stdin/stdout JSON） ===")
    req = {
        "workspaceRoot": workspace,
        "toolName": "read_file",
        "arguments": {"path": "src/main.py"},
        "allowedPaths": [],
    }
    proc = subprocess.run(
        [sys.executable, cli_path],
        input=json.dumps(req),
        text=True,
        capture_output=True,
        timeout=30,
    )
    print(f"stdout: {proc.stdout.strip()}")
    print(f"stderr: {proc.stderr.strip() or '(空)'}")
    print(f"returncode: {proc.returncode}")
    try:
        resp = json.loads(proc.stdout)
        return bool(resp.get("ok"))
    except json.JSONDecodeError:
        return False


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="azur_tools_demo_") as tmp:
        workspace = os.path.join(tmp, "workspace")
        os.makedirs(workspace, exist_ok=True)
        build_workspace(workspace)

        print("=== 工具 JSON Schema ===")
        print(json.dumps(tool_definitions(), ensure_ascii=False, indent=2))

        results = [
            run_scenario("列目录（根目录）", workspace, "list_directory", {"path": "."},
                         expect_ok=True),
            run_scenario("读取文本文件", workspace, "read_file", {"path": "src/main.py"},
                         expect_ok=True),
            run_scenario("读取不存在的文件", workspace, "read_file", {"path": "src/nope.py"},
                         expect_ok=False),
            run_scenario("误用 read_file 读目录", workspace, "read_file", {"path": "src"},
                         expect_ok=False),
            run_scenario("读取二进制文件", workspace, "read_file", {"path": "assets/blob.bin"},
                         expect_ok=False),
            run_scenario("路径穿越（../ 越权，应被拒绝）", workspace, "read_file",
                         {"path": "../outside.txt"}, expect_ok=False),
            run_scenario("不存在的工具", workspace, "run_command", {"cmd": "ls"},
                         expect_ok=False),
        ]

        # 白名单测试：工作区外的文件，未加白名单拒绝、加白名单放行
        outside = os.path.join(tmp, "outside.txt")
        with open(outside, "w", encoding="utf-8") as f:
            f.write("outside content\n")
        results.append(run_scenario("白名单外路径（拒绝）", workspace, "read_file",
                                    {"path": outside}, expect_ok=False))
        results.append(run_scenario("白名单内路径（放行）", workspace, "read_file",
                                    {"path": outside}, allowed=[tmp], expect_ok=True))

        cli_path = os.path.join(_PKG_DIR, "azur_tools", "cli.py")
        results.append(cli_pipe_test(workspace, cli_path))

        passed = sum(1 for r in results if r)
        total = len(results)
        print(f"\n汇总：{passed}/{total} 个场景符合预期"
              f"{'（全部通过）' if passed == total else '（有失败项，请检查）'}")

    print("\n演示结束。")
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
