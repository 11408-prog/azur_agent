"""eval_runner.py 的单元测试（纯标准库 unittest，离线验证评测管线本身）。

覆盖：
- 场景文件能加载（两个文件、kind 正确、必填字段齐全）
- 确定性 judge：contains_facts / no_forbidden_slang / max_chars
- MockBackend 的固定输出
- run_eval 在 mock 模式下 0 失败（评测管线可离线自检）

运行：
    python python/azur_tools/tests/test_eval_runner.py
"""

from __future__ import annotations

import os
import sys
import unittest

# 把 python/ 目录加进 path，让本文件能 import azur_tools
_PKG_DIR = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if _PKG_DIR not in sys.path:
    sys.path.insert(0, _PKG_DIR)

from azur_tools.eval.eval_runner import (  # noqa: E402
    EVAL_DIR,
    MockBackend,
    judge_contains_facts,
    judge_max_chars,
    judge_no_forbidden_slang,
    load_scenarios,
    run_eval,
)


class ScenarioFilesTest(unittest.TestCase):
    def test_memory_scenarios_load(self):
        kind, scenarios = load_scenarios(os.path.join(EVAL_DIR, "scenarios_memory.json"))
        self.assertEqual(kind, "memory")
        self.assertGreaterEqual(len(scenarios), 3)
        ids = [s["id"] for s in scenarios]
        self.assertIn("mem_user_name", ids)
        for s in scenarios:
            self.assertIn("id", s)
            self.assertIn("name", s)
            self.assertIn("messages", s)
            self.assertIn("expect_keys", s)

    def test_tone_scenarios_load(self):
        kind, scenarios = load_scenarios(os.path.join(EVAL_DIR, "scenarios_tone.json"))
        self.assertEqual(kind, "tone")
        self.assertGreaterEqual(len(scenarios), 2)
        ids = [s["id"] for s in scenarios]
        self.assertIn("tone_insult", ids)
        for s in scenarios:
            self.assertIn("prompt", s)
            self.assertIn("judges", s)


class JudgeContainsFactsTest(unittest.TestCase):
    def test_all_expected_present_passes(self):
        facts = [{"key": "user_name", "value": "小明"}]
        ok, msg = judge_contains_facts(facts, ["user_name"], [], [])
        self.assertTrue(ok)
        self.assertEqual(msg, "")

    def test_missing_key_fails(self):
        ok, _ = judge_contains_facts([{"key": "a", "value": "1"}], ["user_name"], [], [])
        self.assertFalse(ok)

    def test_unexpected_key_fails(self):
        ok, _ = judge_contains_facts([{"key": "user_name", "value": "小明"}], [], ["user_name"], [])
        self.assertFalse(ok)

    def test_noise_value_fails(self):
        ok, _ = judge_contains_facts([{"key": "x", "value": "天气不错"}], [], [], ["天气不错"])
        self.assertFalse(ok)


class JudgeSlangTest(unittest.TestCase):
    def test_no_slang_passes(self):
        ok, msg = judge_no_forbidden_slang("早上好，指挥官。", ["草", "卧槽"])
        self.assertTrue(ok)
        self.assertEqual(msg, "")

    def test_slang_fails(self):
        ok, msg = judge_no_forbidden_slang("卧槽，这你也能说", ["草", "卧槽"])
        self.assertFalse(ok)
        self.assertIn("卧槽", msg)


class JudgeMaxCharsTest(unittest.TestCase):
    def test_short_passes(self):
        ok, _ = judge_max_chars("舰载机编队已就绪。", 120)
        self.assertTrue(ok)

    def test_long_fails(self):
        ok, msg = judge_max_chars("啊" * 200, 120)
        self.assertFalse(ok)
        self.assertIn("200", msg)


class MockBackendTest(unittest.TestCase):
    def test_memory_returns_canned_facts(self):
        out = MockBackend().run({"id": "mem_user_name", "kind": "memory"})
        self.assertEqual(out["facts"][0]["key"], "user_name")

    def test_memory_smalltalk_returns_empty(self):
        out = MockBackend().run({"id": "mem_ignore_smalltalk", "kind": "memory"})
        self.assertEqual(out["facts"], [])

    def test_tone_returns_canned_reply(self):
        out = MockBackend().run({"id": "tone_insult", "kind": "tone"})
        self.assertGreater(len(out["text"]), 0)

    def test_judge_passes(self):
        ok, _ = MockBackend().judge("任意文本", "rubric")
        self.assertTrue(ok)


class RunEvalTest(unittest.TestCase):
    def test_mock_mode_zero_failures(self):
        failures, lines = run_eval(MockBackend())
        self.assertEqual(failures, 0)
        joined = "\n".join(lines)
        self.assertIn("记忆抽取", joined)
        self.assertIn("语气一致性", joined)
        self.assertIn("[PASS]", joined)
        self.assertNotIn("[FAIL]", joined)

    def test_filter_limits_scenarios(self):
        failures, lines = run_eval(MockBackend(), filter_substr="mem_")
        self.assertEqual(failures, 0)
        joined = "\n".join(lines)
        self.assertIn("mem_user_name", joined)
        self.assertNotIn("tone_insult", joined)


if __name__ == "__main__":
    unittest.main(verbosity=2)
