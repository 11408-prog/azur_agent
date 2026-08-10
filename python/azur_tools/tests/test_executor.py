"""工具执行器的单元测试（纯标准库 unittest，无 pytest 依赖）。

运行：
    python python/azur_tools/tests/test_executor.py
"""

from __future__ import annotations

import json
import os
import sys
import tempfile
import unittest

# 把 python/ 目录加进 path，让本文件能 import azur_tools
_PKG_DIR = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if _PKG_DIR not in sys.path:
    sys.path.insert(0, _PKG_DIR)

from azur_tools.executor import (  # noqa: E402
    MAX_DIR_ENTRIES,
    MAX_READ_FILE_SIZE,
    execute,
    resolve_safe_path,
    tool_definitions,
)


class ExecutorTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory(prefix="azur_tools_test_")
        self.addCleanup(self._tmp.cleanup)
        self.workspace = os.path.join(self._tmp.name, "workspace")
        os.makedirs(os.path.join(self.workspace, "src"), exist_ok=True)
        with open(os.path.join(self.workspace, "src", "main.py"), "w", encoding="utf-8") as f:
            f.write("# main.py\ndef hello():\n    print('hi')\n")

    # ---- tool_definitions ----
    def test_tool_definitions_names(self):
        names = {t["function"]["name"] for t in tool_definitions()}
        self.assertEqual(names, {"read_file", "list_directory"})

    def test_tool_definitions_serializable(self):
        json.dumps(tool_definitions(), ensure_ascii=False)

    # ---- resolve_safe_path ----
    def test_resolve_inside(self):
        p = resolve_safe_path(self.workspace, "src/main.py", [])
        self.assertIsNotNone(p)
        self.assertTrue(os.path.exists(p))

    def test_resolve_traversal_rejected(self):
        self.assertIsNone(resolve_safe_path(self.workspace, "../x", []))
        self.assertIsNone(resolve_safe_path(self.workspace, "src/../../x", []))
        self.assertIsNone(resolve_safe_path(self.workspace, "..", []))

    def test_resolve_filename_like_parent_allowed(self):
        # "..foo" 是合法文件名，不应误判为穿越
        open(os.path.join(self.workspace, "..foo"), "w").close()
        p = resolve_safe_path(self.workspace, "..foo", [])
        self.assertIsNotNone(p)

    def test_resolve_whitelist(self):
        outside = os.path.join(self._tmp.name, "outside.txt")
        open(outside, "w").close()
        self.assertIsNone(resolve_safe_path(self.workspace, outside, []))
        self.assertEqual(resolve_safe_path(self.workspace, outside, [self._tmp.name]), outside)

    # ---- execute ----
    def test_read_file_ok(self):
        content, ok, label = execute(self.workspace, "read_file", {"path": "src/main.py"})
        self.assertTrue(ok)
        self.assertIn("hello", content)
        self.assertIn("src/main.py", label)
        self.assertIn("行", label)

    def test_read_file_missing(self):
        content, ok, _ = execute(self.workspace, "read_file", {"path": "src/nope.py"})
        self.assertFalse(ok)
        self.assertIn("文件不存在", content)

    def test_read_file_dir(self):
        content, ok, _ = execute(self.workspace, "read_file", {"path": "src"})
        self.assertFalse(ok)
        self.assertIn("是一个目录", content)

    def test_read_file_traversal(self):
        content, ok, _ = execute(self.workspace, "read_file", {"path": "../outside.txt"})
        self.assertFalse(ok)
        self.assertIn("不在工作区目录内", content)

    def test_list_directory_ok(self):
        content, ok, _ = execute(self.workspace, "list_directory", {"path": "."})
        self.assertTrue(ok)
        self.assertIn("[目录] src/", content)
        self.assertEqual(content, "[目录] src/")

    def test_unknown_tool(self):
        content, ok, _ = execute(self.workspace, "run_command", {})
        self.assertFalse(ok)
        self.assertIn("不支持的工具名称", content)

    def test_invalid_workspace(self):
        content, ok, label = execute(os.path.join(self._tmp.name, "nope"), "read_file", {"path": "x"})
        self.assertFalse(ok)
        self.assertEqual(label, "工作区目录无效")


if __name__ == "__main__":
    unittest.main(verbosity=2)
