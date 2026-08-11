"""memory_cli.py 的单元测试（纯标准库 unittest，不联网、不依赖真实 LLM）。

覆盖 memory_cli.py 的纯函数与 main() 协议流：
- chat_completions_url / build_system_prompt / format_messages
- normalize_facts / _extract_json_object（LLM 输出鲁棒解析，整个记忆系统最易坏的环节）
- main()：mock call_llm，验证成功 / 各类失败路径下的 stdout JSON 协议

运行：
    python python/azur_tools/tests/test_memory_cli.py
"""

from __future__ import annotations

import contextlib
import io
import json
import os
import sys
import unittest
from unittest import mock

# 把 python/ 目录加进 path，让本文件能 import azur_tools
_PKG_DIR = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if _PKG_DIR not in sys.path:
    sys.path.insert(0, _PKG_DIR)

from azur_tools import memory_cli  # noqa: E402


def _req(**overrides):
    """构造一份合法的请求 JSON（可覆盖字段）。"""
    base = {
        "apiKey": "sk-test",
        "baseUrl": "https://api.deepseek.com",
        "model": "deepseek-v4-flash",
        "messages": [{"role": "user", "content": "我叫小明，喜欢龙井"}],
        "existingFacts": [{"key": "user_name", "value": "小明"}],
    }
    base.update(overrides)
    return base


def _llm_response(content):
    """构造 OpenAI 兼容的 chat/completions 响应。"""
    return {"choices": [{"message": {"content": content}}]}


def run_main(stdin_text, llm=None, llm_error=None):
    """以指定 stdin 文本调用 memory_cli.main()，返回 (exit_code, stdout文本)。

    llm: 传给 mock call_llm 的 return_value；llm_error 非 None 时改为抛异常。
    """
    if llm_error is not None:
        side_effect = mock.Mock(side_effect=llm_error)
    else:
        side_effect = mock.Mock(return_value=llm)
    out = io.StringIO()
    with mock.patch("sys.stdin", io.StringIO(stdin_text)), \
         mock.patch("azur_tools.memory_cli.call_llm", side_effect), \
         contextlib.redirect_stdout(out):
        code = memory_cli.main([])
    return code, out.getvalue()


class ChatCompletionsUrlTest(unittest.TestCase):
    def test_plain_base_url_appends_endpoint(self):
        self.assertEqual(
            memory_cli.chat_completions_url("https://api.deepseek.com"),
            "https://api.deepseek.com/chat/completions",
        )

    def test_trailing_slash_normalized(self):
        self.assertEqual(
            memory_cli.chat_completions_url("https://api.deepseek.com/"),
            "https://api.deepseek.com/chat/completions",
        )

    def test_multiple_trailing_slashes_normalized(self):
        self.assertEqual(
            memory_cli.chat_completions_url("https://api.deepseek.com///"),
            "https://api.deepseek.com/chat/completions",
        )

    def test_already_has_endpoint_not_duplicated(self):
        self.assertEqual(
            memory_cli.chat_completions_url("https://api.deepseek.com/v1/chat/completions"),
            "https://api.deepseek.com/v1/chat/completions",
        )

    def test_whitespace_trimmed(self):
        self.assertEqual(
            memory_cli.chat_completions_url("  https://api.deepseek.com  "),
            "https://api.deepseek.com/chat/completions",
        )


class BuildSystemPromptTest(unittest.TestCase):
    def test_includes_existing_facts(self):
        prompt = memory_cli.build_system_prompt([{"key": "user_name", "value": "小明"}])
        self.assertIn("user_name", prompt)
        self.assertIn("小明", prompt)
        self.assertIn("已知事实", prompt)

    def test_no_existing_facts_placeholder(self):
        prompt = memory_cli.build_system_prompt([])
        self.assertIn("（无）", prompt)

    def test_contains_output_format_spec(self):
        prompt = memory_cli.build_system_prompt([])
        self.assertIn('{"facts":', prompt)
        self.assertIn("confidence", prompt)


class FormatMessagesTest(unittest.TestCase):
    def test_empty_returns_placeholder(self):
        self.assertEqual(memory_cli.format_messages([], 10), "（无对话内容）")

    def test_role_labels_mapped(self):
        msgs = [
            {"role": "user", "content": "你好"},
            {"role": "assistant", "content": "我是企业"},
            {"role": "system", "content": "设定"},
        ]
        out = memory_cli.format_messages(msgs, 10)
        self.assertIn("用户: 你好", out)
        self.assertIn("企业: 我是企业", out)
        self.assertIn("system: 设定", out)

    def test_max_messages_keeps_only_last_n(self):
        msgs = [{"role": "user", "content": f"第{i}条"} for i in range(5)]
        out = memory_cli.format_messages(msgs, 2)
        self.assertNotIn("第0条", out)
        self.assertNotIn("第1条", out)
        self.assertIn("第3条", out)
        self.assertIn("第4条", out)

    def test_long_content_truncated(self):
        long_content = "长" * (memory_cli.MAX_MSG_CHARS + 50)
        out = memory_cli.format_messages([{"role": "user", "content": long_content}], 10)
        self.assertIn("[truncated]", out)
        self.assertLess(len(out), len(long_content))


class ExtractJsonObjectTest(unittest.TestCase):
    """_extract_json_object 是记忆系统最脆的一环，重点覆盖。"""

    def test_plain_json(self):
        self.assertEqual(memory_cli._extract_json_object('{"a": 1}'), {"a": 1})

    def test_fenced_json(self):
        self.assertEqual(
            memory_cli._extract_json_object('```json\n{"facts": []}\n```'),
            {"facts": []},
        )

    def test_prose_wrapped_brace_slice(self):
        self.assertEqual(
            memory_cli._extract_json_object('好的，抽取结果如下：{"a": 1}，以上。'),
            {"a": 1},
        )

    def test_trailing_comma_repaired(self):
        self.assertEqual(
            memory_cli._extract_json_object('{"a": 1, "b": [1, 2,],}'),
            {"a": 1, "b": [1, 2]},
        )

    def test_empty_returns_none(self):
        self.assertIsNone(memory_cli._extract_json_object(""))
        self.assertIsNone(memory_cli._extract_json_object("   "))

    def test_not_json_returns_none(self):
        self.assertIsNone(memory_cli._extract_json_object("这不是JSON"))

    def test_array_is_rejected(self):
        # 只接受 dict 对象，数组不作为结果
        self.assertIsNone(memory_cli._extract_json_object("[1, 2, 3]"))

    def test_fenced_without_language_tag(self):
        self.assertEqual(
            memory_cli._extract_json_object('```\n{"ok": true}\n```'),
            {"ok": True},
        )


class NormalizeFactsTest(unittest.TestCase):
    def test_valid_facts_confidence_preserved(self):
        payload = {"facts": [{"key": "preference_tea", "value": "喜欢龙井", "confidence": 0.8}]}
        self.assertEqual(
            memory_cli.normalize_facts(payload),
            [{"key": "preference_tea", "value": "喜欢龙井", "confidence": 0.8}],
        )

    def test_empty_value_and_key_skipped(self):
        payload = {
            "facts": [
                {"key": "empty_value", "value": "  "},
                {"key": "", "value": "有值没key"},
                {"key": "ok", "value": "正常"},
            ]
        }
        self.assertEqual(memory_cli.normalize_facts(payload), [{"key": "ok", "value": "正常"}])

    def test_not_dict_payload(self):
        self.assertEqual(memory_cli.normalize_facts("字符串"), [])
        self.assertEqual(memory_cli.normalize_facts(None), [])

    def test_facts_not_a_list(self):
        self.assertEqual(memory_cli.normalize_facts({"facts": "不是数组"}), [])

    def test_non_dict_entries_skipped(self):
        payload = {"facts": ["字符串", 123, {"key": "a", "value": "b"}]}
        self.assertEqual(memory_cli.normalize_facts(payload), [{"key": "a", "value": "b"}])

    def test_key_value_whitespace_stripped(self):
        payload = {"facts": [{"key": "  user_name  ", "value": "  小明  "}]}
        self.assertEqual(memory_cli.normalize_facts(payload), [{"key": "user_name", "value": "小明"}])

    def test_confidence_only_kept_if_number(self):
        payload = {
            "facts": [
                {"key": "a", "value": "1", "confidence": "高"},  # 非数字：事实保留，confidence 丢弃
                {"key": "b", "value": "2", "confidence": 0.5},   # 数字：confidence 保留
            ]
        }
        self.assertEqual(
            memory_cli.normalize_facts(payload),
            [{"key": "a", "value": "1"}, {"key": "b", "value": "2", "confidence": 0.5}],
        )


class MainSuccessTest(unittest.TestCase):
    def test_success_returns_facts_and_ok(self):
        llm = _llm_response(
            '{"facts": [{"key": "preference_tea", "value": "喜欢龙井", "confidence": 0.8}]}'
        )
        code, stdout = run_main(json.dumps(_req(), ensure_ascii=False), llm=llm)
        self.assertEqual(code, 0)
        obj = json.loads(stdout)
        self.assertTrue(obj["ok"])
        facts = json.loads(obj["content"])
        self.assertEqual(facts, [{"key": "preference_tea", "value": "喜欢龙井", "confidence": 0.8}])

    def test_success_builds_prompt_and_payload(self):
        llm = _llm_response('{"facts": []}')
        req = _req()
        with mock.patch("azur_tools.memory_cli.call_llm", return_value=llm) as m, \
             mock.patch("sys.stdin", io.StringIO(json.dumps(req, ensure_ascii=False))), \
             contextlib.redirect_stdout(io.StringIO()):
            code = memory_cli.main([])
        self.assertEqual(code, 0)
        m.assert_called_once()
        api_key, base_url, model, system, user_content = m.call_args[0]
        self.assertEqual(api_key, "sk-test")
        self.assertEqual(base_url, "https://api.deepseek.com")
        self.assertEqual(model, "deepseek-v4-flash")
        self.assertIn("user_name", system)      # system prompt 含已有事实
        self.assertIn("我叫小明", user_content)  # user content 含对话

    def test_success_prose_wrapped_llm_output(self):
        # LLM 输出带散文和代码围栏，_extract_json_object 应能剥出来
        llm = _llm_response('好的：\n```json\n{"facts": [{"key": "x", "value": "y"}]}\n```\n完毕')
        code, stdout = run_main(json.dumps(_req(), ensure_ascii=False), llm=llm)
        self.assertEqual(code, 0)
        obj = json.loads(stdout)
        self.assertTrue(obj["ok"])
        self.assertEqual(json.loads(obj["content"]), [{"key": "x", "value": "y"}])


class MainErrorPathTest(unittest.TestCase):
    def test_empty_stdin(self):
        code, stdout = run_main("")
        self.assertEqual(code, 1)
        self.assertEqual(json.loads(stdout)["ok"], False)
        self.assertIn("空请求", stdout)

    def test_invalid_json(self):
        code, stdout = run_main("这不是JSON{{{")
        self.assertEqual(code, 1)
        self.assertEqual(json.loads(stdout)["ok"], False)
        self.assertIn("不是合法 JSON", stdout)

    def test_missing_api_key(self):
        code, stdout = run_main(json.dumps(_req(apiKey="  "), ensure_ascii=False))
        self.assertEqual(code, 1)
        self.assertIn("apiKey 为空", stdout)

    def test_missing_base_url(self):
        code, stdout = run_main(json.dumps(_req(baseUrl=""), ensure_ascii=False))
        self.assertEqual(code, 1)
        self.assertIn("baseUrl 为空", stdout)

    def test_missing_model(self):
        code, stdout = run_main(json.dumps(_req(model=""), ensure_ascii=False))
        self.assertEqual(code, 1)
        self.assertIn("model 为空", stdout)

    def test_llm_network_error(self):
        code, stdout = run_main(
            json.dumps(_req(), ensure_ascii=False),
            llm_error=RuntimeError("连接超时"),
        )
        self.assertEqual(code, 1)
        self.assertIn("调用模型失败", stdout)
        self.assertIn("连接超时", stdout)

    def test_response_missing_content(self):
        code, stdout = run_main(
            json.dumps(_req(), ensure_ascii=False),
            llm={"choices": [{"message": {}}]},
        )
        self.assertEqual(code, 1)
        self.assertIn("缺少 choices[0].message.content", stdout)

    def test_llm_output_not_parseable(self):
        code, stdout = run_main(
            json.dumps(_req(), ensure_ascii=False),
            llm=_llm_response("抱歉，我无法生成 JSON"),
        )
        self.assertEqual(code, 1)
        self.assertIn("无法从模型输出中解析 JSON", stdout)

    def test_max_messages_invalid_falls_back_to_default(self):
        # 非法 maxMessages 不应导致崩溃，应走默认值
        llm = _llm_response('{"facts": []}')
        code, stdout = run_main(
            json.dumps(_req(maxMessages="不是数字"), ensure_ascii=False),
            llm=llm,
        )
        self.assertEqual(code, 0)
        self.assertTrue(json.loads(stdout)["ok"])


class CallLlmTest(unittest.TestCase):
    """call_llm 的 HTTP 细节（mock urllib，不联网）。"""

    def test_builds_request_and_parses_response(self):
        class FakeResp:
            def read(self):
                return b'{"ok": true, "x": 1}'

            def __enter__(self):
                return self

            def __exit__(self, *exc):
                return False

        captured = {}

        def fake_urlopen(req, timeout):
            captured["url"] = req.full_url
            captured["method"] = req.method
            captured["auth"] = req.get_header("Authorization")
            captured["ctype"] = req.get_header("Content-type")
            captured["timeout"] = timeout
            captured["body"] = req.data
            return FakeResp()

        with mock.patch("urllib.request.urlopen", side_effect=fake_urlopen) as m:
            result = memory_cli.call_llm(
                "sk-test", "https://api.deepseek.com/", "deepseek-v4-flash",
                "system prompt", "user content",
            )
        self.assertEqual(result, {"ok": True, "x": 1})
        self.assertEqual(captured["url"], "https://api.deepseek.com/chat/completions")
        self.assertEqual(captured["method"], "POST")
        self.assertEqual(captured["auth"], "Bearer sk-test")
        self.assertEqual(captured["ctype"], "application/json")
        self.assertEqual(captured["timeout"], 60)
        body = json.loads(captured["body"].decode("utf-8"))
        self.assertEqual(body["model"], "deepseek-v4-flash")
        self.assertEqual(body["stream"], False)
        self.assertEqual(body["temperature"], 0.2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
