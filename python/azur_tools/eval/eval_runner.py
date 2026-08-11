"""Azur Agent AI 评测骨架（T3）。

评估模型在两类核心能力上的表现，供修改记忆抽取 / 语气一致性后回归：

- memory：把对话喂给记忆抽取后端，检查抽取的事实是否命中预期的 key、
          是否引入不该有的噪音（对应 C++ 侧 memory_cli 通道）。
- tone  ：把 prompt 喂给「企业」角色后端，检查回复是否保持人设
          （不崩人设、克制简短、LLM 复核）。

双后端：
- mock（默认，离线）：内置固定输出，用于验证评测管线本身
  （场景加载 / judge / 报告），不联网、不需要 API key。
- LLM （--api-key 等）：真实调用 OpenAI 兼容 chat/completions，
  做真实评测（复用 memory_cli 的 urllib 实现，零第三方依赖）。

judge 说明：
- contains_facts     （memory）：期望的 key 都在、不该出现的 key/value 都不在。
- no_forbidden_slang （tone）  ：回复不含违禁词（崩人设的粗话 / 网络语 / 颜文字）。
- max_chars          （tone）  ：回复长度不超阈值（克制简短）。
- llm_as_judge       （tone）  ：把回复 + rubric 交给 LLM 打分，
                                 输出 {"pass": bool, "reason": str}。

用法：
    python python/azur_tools/eval/eval_runner.py [--mock] [--verbose]
    python python/azur_tools/eval/eval_runner.py \
        --api-key sk-xxx --base-url https://api.deepseek.com --model deepseek-v4-flash \
        [--filter tone] [--verbose]

退出码 = 失败场景数（0 表示全部通过），便于接入 CI / pre-commit。
"""

from __future__ import annotations

import argparse
import json
import os
import sys

# 允许直接运行本文件（python .../eval/eval_runner.py）时 import 到 azur_tools。
# 3 层 dirname：eval/ -> azur_tools/ -> python/，python/ 才是包根。
_PKG_DIR = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if _PKG_DIR not in sys.path:
    sys.path.insert(0, _PKG_DIR)

from azur_tools import memory_cli  # noqa: E402

EVAL_DIR = os.path.dirname(os.path.abspath(__file__))

# 语气评测里「企业」的角色 system prompt（与 C++ 侧人格设定对齐：沉稳、克制、军人式冷静）。
TONE_SYSTEM = (
    "你是《碧蓝航线》中的航空母舰「企业」（Enterprise），一支舰队的旗舰，也是指挥官的得力助手。\n"
    "性格：沉稳、克制、军人式冷静；对指挥官保持尊敬与距离感，说话简短干脆，不撒娇、不谄媚。\n"
    "要求：始终以第一人称「我」回应，保持人设；不爆粗口、不用网络流行语、不发颜文字；\n"
    "回复长度克制（通常 1~3 句）。无论对方说什么，都保持冷静得体的军人姿态。"
)


def load_scenarios(path: str):
    """读取一个场景文件，返回 (kind, scenarios 列表)。

    把文件级的 kind 注入每个场景 dict（后端与 judge 都靠 scenario["kind"] 分派）。
    """
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    kind = data["kind"]
    scenarios = []
    for sc in data.get("scenarios", []):
        sc["kind"] = kind
        scenarios.append(sc)
    return kind, scenarios


# ---------------------------------------------------------------- judges

def judge_contains_facts(facts, expect_keys, expect_no_keys, expect_no_values):
    """memory 类 judge：期望的 key 都抽到；不该出现的 key / value 都没抽到。"""
    keys = {str(f.get("key", "")).strip() for f in facts}
    values = {str(f.get("value", "")).strip() for f in facts}

    missing = [k for k in expect_keys if k not in keys]
    unexpected_keys = [k for k in expect_no_keys if k in keys]
    unexpected_values = [v for v in expect_no_values if v in values]

    problems = []
    if missing:
        problems.append("缺少期望 key: " + ", ".join(missing))
    if unexpected_keys:
        problems.append("出现不该有的 key: " + ", ".join(unexpected_keys))
    if unexpected_values:
        problems.append("抽取了噪音值: " + ", ".join(unexpected_values))
    return (not problems, "; ".join(problems))


def judge_no_forbidden_slang(text: str, forbidden):
    """tone 类 judge：回复不含违禁词。"""
    found = [w for w in forbidden if w in text]
    if found:
        return False, "回复含违禁词: " + ", ".join(found)
    return True, ""


def judge_max_chars(text: str, max_chars: int):
    """tone 类 judge：回复长度克制（字符数上限）。"""
    n = len(text)
    if n > max_chars:
        return False, f"回复过长：{n} 字 > 上限 {max_chars} 字"
    return True, ""


# ---------------------------------------------------------------- backends

class MockBackend:
    """离线后端：内置固定输出，让评测管线不依赖网络也能跑通并验证 judge 逻辑。"""

    name = "mock"

    MEMORY_FACTS = {
        "mem_user_name": [{"key": "user_name", "value": "小明", "confidence": 0.9}],
        "mem_preference_tea": [{"key": "preference_tea", "value": "喜欢龙井茶", "confidence": 0.9}],
        "mem_promise": [{"key": "promise", "value": "周末一起去看海", "confidence": 0.8}],
        "mem_ignore_smalltalk": [],
    }

    TONE_REPLIES = {
        "tone_insult": "指挥官，请注意你的言辞。我的战绩自有舰队来评价，不需要你定义。",
        "tone_morning": "早上好，指挥官。舰队一切正常，随时可以出击。",
        "tone_action": "舰载机编队已就绪，建议先派侦察机确认敌方动向。",
    }

    def run(self, scenario):
        if scenario["kind"] == "memory":
            return {"facts": self.MEMORY_FACTS.get(scenario["id"], [])}
        return {"text": self.TONE_REPLIES.get(scenario["id"], "（默认回复）")}

    def judge(self, text, rubric):
        # mock 模式不真调 LLM，直接判通过（人设合规性由确定性 judge 兜底）
        return True, "（mock judge 默认通过）"


class LLMBackend:
    """真实后端：调用 OpenAI 兼容 chat/completions（复用 memory_cli 的 HTTP 实现）。"""

    def __init__(self, api_key: str, base_url: str, model: str):
        self.api_key = api_key
        self.base_url = base_url
        self.model = model
        self.name = f"llm({model})"

    def run(self, scenario):
        if scenario["kind"] == "memory":
            system = memory_cli.build_system_prompt([])
            user = "=== 最近对话 ===\n" + memory_cli.format_messages(scenario["messages"], 10)
        else:
            system = TONE_SYSTEM
            user = scenario["prompt"]
        resp = memory_cli.call_llm(self.api_key, self.base_url, self.model, system, user)
        content = resp["choices"][0]["message"]["content"]
        if scenario["kind"] == "memory":
            payload = memory_cli._extract_json_object(content)
            return {"facts": memory_cli.normalize_facts(payload or {})}
        return {"text": content.strip()}

    def judge(self, text, rubric):
        system = (
            "你是一个严格的评测员。给定一段「企业」角色的回复和一条人设要求，判断该回复是否"
            "符合要求。只输出一个 JSON 对象，格式：{\"pass\": true/false, \"reason\": \"一句话理由\"}"
        )
        user = f"人设要求：{rubric}\n\n企业回复：\n{text}"
        resp = memory_cli.call_llm(self.api_key, self.base_url, self.model, system, user)
        content = resp["choices"][0]["message"]["content"]
        verdict = memory_cli._extract_json_object(content) or {}
        passed = bool(verdict.get("pass"))
        reason = str(verdict.get("reason", "")).strip()
        return passed, reason


# ---------------------------------------------------------------- pipeline

def _judge_scenario(scenario, output, backend):
    """应用场景声明的 judge，返回 (ok, reasons 列表)。"""
    reasons = []

    if scenario["kind"] == "memory":
        ok, msg = judge_contains_facts(
            output.get("facts", []),
            scenario.get("expect_keys", []),
            scenario.get("expect_no_keys", []),
            scenario.get("expect_no_values", []),
        )
        if msg:
            reasons.append(msg)
        return ok, reasons

    # tone：按场景声明的 judges 依次应用
    text = output.get("text", "")
    judges = scenario.get("judges", ["no_forbidden_slang", "max_chars"])
    ok = True
    for j in judges:
        if j == "no_forbidden_slang":
            passed, msg = judge_no_forbidden_slang(text, scenario.get("forbidden", []))
        elif j == "max_chars":
            passed, msg = judge_max_chars(text, scenario.get("max_chars", 200))
        elif j == "llm_as_judge":
            passed, msg = backend.judge(text, scenario.get("rubric", ""))
        else:
            passed, msg = False, f"未知 judge: {j}"
        if not passed:
            ok = False
        if msg:
            reasons.append(f"[{j}] {msg}")
    return ok, reasons


def run_eval(backend, filter_substr=None, verbose=False):
    """跑完所有场景，返回 (failures 数, 报告行列表)。"""
    lines = []
    failures = 0
    passed = 0
    total = 0

    lines.append("========== Azur Agent AI 评测 ==========")
    lines.append(f"后端: {backend.name}")
    lines.append("")

    for file_name in ("scenarios_memory.json", "scenarios_tone.json"):
        path = os.path.join(EVAL_DIR, file_name)
        kind, scenarios = load_scenarios(path)
        section = "记忆抽取" if kind == "memory" else "语气一致性"
        lines.append(f"---- {section} ({kind}) ----")
        for sc in scenarios:
            sid = sc["id"]
            if filter_substr and filter_substr not in sid:
                continue
            total += 1
            output = backend.run(sc)
            ok, reasons = _judge_scenario(sc, output, backend)
            mark = "[PASS]" if ok else "[FAIL]"
            lines.append(f"{mark} {sid:<22} {sc['name']}")
            if ok:
                passed += 1
            else:
                failures += 1
            if verbose and reasons:
                for r in reasons:
                    lines.append(f"        - {r}")
        lines.append("")

    lines.append(f"通过 {passed} / {total}，失败 {failures}")
    return failures, lines


def main(argv=None) -> int:
    argv = argv if argv is not None else sys.argv[1:]
    parser = argparse.ArgumentParser(description="Azur Agent AI 评测（记忆抽取 + 语气一致性）")
    parser.add_argument("--mock", action="store_true", help="离线 mock 模式（默认）")
    parser.add_argument("--api-key", help="LLM API 密钥（提供则进入真实模式）")
    parser.add_argument("--base-url", default="https://api.deepseek.com", help="OpenAI 兼容 Base URL")
    parser.add_argument("--model", default="deepseek-v4-flash", help="模型名")
    parser.add_argument("--filter", default=None, help="只跑 id 包含该子串的场景")
    parser.add_argument("--verbose", "-v", action="store_true", help="打印每个失败的 judge 理由")
    args = parser.parse_args(argv)

    if args.mock or not args.api_key:
        backend = MockBackend()
    else:
        backend = LLMBackend(args.api_key, args.base_url, args.model)

    failures, lines = run_eval(backend, filter_substr=args.filter, verbose=args.verbose)
    print("\n".join(lines))
    return failures


if __name__ == "__main__":
    sys.exit(main())
