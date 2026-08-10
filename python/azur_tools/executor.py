"""工具执行器：read_file / list_directory 两个只读工具（纯标准库，无 Qt 依赖）。

行为与 core/tool_executor.cpp 完全对齐：
- 所有路径强制限制在"工作区目录"（或白名单）内，越权访问一律拒绝；
- 单文件最大 300KB，疑似二进制文件拒绝读取；
- 单次列目录最多返回 200 条。

设计原则：尽可能独立——不 import 任何第三方库，不依赖 C++/Qt，
可单独测试，也可作为独立进程（stdio JSON 协议）被 C++ 侧 QProcess 调用。
"""

from __future__ import annotations

import os
from typing import Any, Dict, List, Optional, Tuple

# 与 core/tool_executor.cpp 对齐的限制
MAX_READ_FILE_SIZE = 300 * 1024   # 单文件最大 300KB
MAX_DIR_ENTRIES = 200             # 单次列目录最多返回的条目数


def _looks_binary(data: bytes) -> bool:
    """粗略判断是否为二进制文件：抽样检查前若干字节里有没有 NUL 字符。"""
    return b"\x00" in data[:4096]


def _is_inside(dir_path: str, target: str) -> bool:
    """判断 target 是否在 dir_path 内部（含 dir 本身）。跨平台安全判断。

    对应 C++ 里 ToolExecutor::resolveSafePath 的 isInside lambda：
    用 relpath 判断，不使用 "/" 字符串拼接，避免盘符/分隔符差异导致误判。
    """
    if target == dir_path:
        return True
    try:
        rel = os.path.relpath(target, dir_path).replace("\\", "/")
    except ValueError:
        # 跨盘符无法相对化，判定为越界
        return False
    if rel == ".":
        return True
    # 以 "../" 开头或就是 ".." → 在工作区外。
    # 注意："..foo" 这种合法文件名不会命中（不以 "/" 结尾）。
    if rel == ".." or rel.startswith("../"):
        return False
    return True


def resolve_safe_path(workspace_root: str,
                      relative_path: str,
                      allowed_paths: Optional[List[str]] = None) -> Optional[str]:
    """把"相对路径"解析成绝对路径，并确保结果没有越出 workspace_root（防路径穿越）。

    返回安全的目标绝对路径；根目录不存在或越界（含不在白名单内）时返回 None。
    """
    allowed_paths = allowed_paths or []
    if not os.path.isdir(workspace_root):
        return None
    canonical_root = os.path.realpath(workspace_root)

    rel = (relative_path or "").strip()
    if not rel:
        rel = "."

    combined = os.path.join(canonical_root, rel)
    cleaned = os.path.normpath(combined)
    target = os.path.realpath(cleaned) if os.path.exists(cleaned) else cleaned

    if _is_inside(canonical_root, target):
        return target

    # 不在根目录内，检查白名单（同样的跨平台安全判断）
    for allowed in allowed_paths:
        if not os.path.exists(allowed):
            continue
        canonical_allowed = os.path.realpath(allowed)
        if _is_inside(canonical_allowed, target):
            return target

    return None


def tool_definitions() -> List[Dict[str, Any]]:
    """返回工具的 JSON Schema 定义（OpenAI/DeepSeek function calling 格式）。"""
    return [
        {
            "type": "function",
            "function": {
                "name": "read_file",
                "description": (
                    "读取工作区内某个文本文件的完整内容。仅限工作区目录内的路径，"
                    "单文件最大 300KB，超出大小或疑似二进制文件会被拒绝。"
                ),
                "parameters": {
                    "type": "object",
                    "properties": {
                        "path": {
                            "type": "string",
                            "description": "相对于工作区根目录的文件路径，例如 src/mainwindow.cpp",
                        }
                    },
                    "required": ["path"],
                },
            },
        },
        {
            "type": "function",
            "function": {
                "name": "list_directory",
                "description": (
                    "列出工作区内某个目录下的文件和子目录（只列一层，不递归）。"
                    "仅限工作区目录内的路径。"
                ),
                "parameters": {
                    "type": "object",
                    "properties": {
                        "path": {
                            "type": "string",
                            "description": "相对于工作区根目录的目录路径，传空字符串或 \".\" 表示工作区根目录本身",
                        }
                    },
                    "required": ["path"],
                },
            },
        },
    ]


def is_write_tool(tool_name: str) -> bool:
    """当前工具集里只有只读工具，恒返回 False。保留此函数仅为了让调用方接口对齐。"""
    return False


def _read_file(workspace_root: str, args: Dict[str, Any],
               allowed_paths: List[str]) -> Tuple[str, bool, str]:
    rel_path = str(args.get("path") or "")
    display_label = f"读取 {rel_path if rel_path else '(空路径)'}"

    if not rel_path:
        return "错误：未提供 path 参数", False, display_label

    full_path = resolve_safe_path(workspace_root, rel_path, allowed_paths)
    if full_path is None:
        return f"错误：路径 \"{rel_path}\" 不在工作区目录内，已拒绝访问", False, display_label

    if not os.path.exists(full_path):
        return f"错误：文件不存在: {rel_path}", False, display_label
    if os.path.isdir(full_path):
        return f"错误：\"{rel_path}\" 是一个目录，请改用 list_directory", False, display_label
    if os.path.getsize(full_path) > MAX_READ_FILE_SIZE:
        size_kb = os.path.getsize(full_path) // 1024
        return f"错误：文件过大（约 {size_kb} KB），超过 300KB 上限，无法读取", False, display_label

    try:
        with open(full_path, "rb") as f:
            data = f.read()
    except OSError:
        return f"错误：无法打开文件: {rel_path}", False, display_label

    if _looks_binary(data):
        return f"错误：\"{rel_path}\" 看起来是二进制文件，无法作为文本读取", False, display_label

    # QString::fromUtf8 对非法 UTF-8 用替换字符兜底，这里用 errors="replace" 对齐
    text = data.decode("utf-8", errors="replace")
    line_count = text.count("\n") + 1
    return text, True, f"读取 {rel_path} ({line_count} 行)"


def _list_directory(workspace_root: str, args: Dict[str, Any],
                    allowed_paths: List[str]) -> Tuple[str, bool, str]:
    rel_path = str(args.get("path") or "").strip()
    if not rel_path:
        rel_path = "."
    display_label = f"列出目录 {rel_path}"

    full_path = resolve_safe_path(workspace_root, rel_path, allowed_paths)
    if full_path is None:
        return f"错误：路径 \"{rel_path}\" 不在工作区目录内，已拒绝访问", False, display_label
    if not os.path.isdir(full_path):
        return f"错误：\"{rel_path}\" 不是一个有效目录", False, display_label

    try:
        names = os.listdir(full_path)
    except OSError:
        return f"错误：无法打开目录: {rel_path}", False, display_label

    # 排序：目录优先，其次按名称（与 C++ QDir::DirsFirst | QDir::Name 对齐）
    items = []
    for name in names:
        if name in (".", ".."):
            continue
        p = os.path.join(full_path, name)
        is_dir = os.path.isdir(p)
        size = os.path.getsize(p) if os.path.isfile(p) else 0
        items.append((name, is_dir, size))
    items.sort(key=lambda item: (not item[1], item[0].lower()))

    lines: List[str] = []
    count = 0
    for name, is_dir, size in items:
        if count >= MAX_DIR_ENTRIES:
            lines.append(f"... 还有更多条目未显示（超过 {MAX_DIR_ENTRIES} 个上限）")
            break
        if is_dir:
            lines.append(f"[目录] {name}/")
        else:
            lines.append(f"[文件] {name} ({size} 字节)")
        count += 1

    content = "(空目录)" if not lines else "\n".join(lines)
    return content, True, f"列出目录 {rel_path} ({count} 项)"


def execute(workspace_root: str,
            tool_name: str,
            arguments: Dict[str, Any],
            allowed_paths: Optional[List[str]] = None) -> Tuple[str, bool, str]:
    """执行一次工具调用。

    返回 (content, ok, display_label)：
        content      返回值会原样作为 role:"tool" 消息的 content 回传给模型
        ok           是否执行成功
        display_label 用于步骤指示器展示的简短描述
    """
    allowed_paths = allowed_paths or []
    arguments = arguments or {}

    if not workspace_root or not os.path.isdir(workspace_root):
        return "错误：工作区目录不存在，请先在设置页选择一个有效目录", False, "工作区目录无效"

    if tool_name == "read_file":
        return _read_file(workspace_root, arguments, allowed_paths)
    if tool_name == "list_directory":
        return _list_directory(workspace_root, arguments, allowed_paths)

    return f"错误：不支持的工具名称: {tool_name}", False, f"未知工具: {tool_name}"
