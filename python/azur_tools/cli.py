"""stdin/stdout JSON 协议入口 —— C++ 侧 QProcess 调用工具的缝，也可在终端直接调试。

请求（stdin 一行 JSON）：
    {"workspaceRoot": "...", "toolName": "read_file",
     "arguments": {"path": "src/main.py"}, "allowedPaths": [...]}

响应（stdout 一行 JSON，保证可被 json.loads 解析）：
    {"ok": true, "content": "...", "displayLabel": "..."}

stdout 只写这一个 JSON 对象，绝不写其它输出（防止污染协议）；
所有诊断信息走 stderr。

用法：
    python cli.py --tool-definitions            # 打印工具 JSON Schema
    echo '{"workspaceRoot":"...",...}' | python cli.py
"""

from __future__ import annotations

import json
import os
import sys

# 协议要求：stdin/stdout 统一按 UTF-8 收发。
# Windows 下 Python 默认用系统代码页（GBK）读写文本流，而 C++ 侧
# QJsonDocument 按 UTF-8 解析 —— 不强制的话返回 JSON 里的中文会被编成
# GBK 字节，导致解析失败（后端实际工作正常却报「无法解析的响应」）。
if hasattr(sys.stdin, "reconfigure"):
    try:
        sys.stdin.reconfigure(encoding="utf-8")
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError, OSError):
        pass

# 兼容两种运行方式：
#   python -m azur_tools.cli         （在 python/ 目录下，走包相对导入）
#   python python/azur_tools/cli.py  （直接当脚本跑，把自己所在目录加进 path）
if __package__ in (None, ""):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from executor import execute, tool_definitions
else:
    from .executor import execute, tool_definitions


def _write_json(obj) -> None:
    sys.stdout.write(json.dumps(obj, ensure_ascii=False))
    sys.stdout.write("\n")
    sys.stdout.flush()


def main(argv=None) -> int:
    argv = argv if argv is not None else sys.argv

    if "--tool-definitions" in argv:
        _write_json(tool_definitions())
        return 0

    raw = sys.stdin.read()
    if not raw.strip():
        _write_json({"ok": False, "content": "错误：空请求", "displayLabel": "解析失败"})
        return 1

    try:
        req = json.loads(raw)
    except json.JSONDecodeError:
        _write_json({"ok": False, "content": "错误：请求不是合法 JSON", "displayLabel": "解析失败"})
        return 1

    content, ok, label = execute(
        req.get("workspaceRoot", ""),
        req.get("toolName", ""),
        req.get("arguments") or {},
        req.get("allowedPaths") or [],
    )
    _write_json({"ok": ok, "content": content, "displayLabel": label})
    return 0


if __name__ == "__main__":
    sys.exit(main())
