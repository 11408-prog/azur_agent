"""LLM 事实抽取 CLI —— stdin/stdout JSON 协议，与 cli.py / tts_cli.py 同款约定。

请求（stdin 一行 JSON）：
    {
      "apiKey": "...",          // 调用 chat/completions 用的密钥
      "baseUrl": "https://api.deepseek.com",  // 与设置页一致，会自动补 /chat/completions
      "model": "deepseek-v4-flash",
      "messages": [{"role":"user","content":"..."}, ...],   // 最近对话（含本轮 user+assistant）
      "existingFacts": [{"key":"user_name","value":"小明"}, ...],  // 可选：已知事实，减少重复抽取
      "maxMessages": 10         // 可选：送入 LLM 的最近消息条数上限（默认 10）
    }

响应（stdout 一行 JSON，保证可被 json.loads 解析）：
    成功 {"ok": true, "content": "[{\"key\":\"...\",\"value\":\"...\"}, ...]"}
    失败 {"ok": false, "content": "<错误描述>"}

本脚本只做两件事：把对话交给 LLM 抽取「值得长期记住的事实」、把 LLM 返回内容
鲁棒地解析成 JSON 数组。事实与已有 facts.json 的合并 / 去重 / 原子写入由 C++ 侧
（MemoryClient）负责——这样合并逻辑可以在单元测试里用真实文件覆盖。

stdout 只写这一个 JSON 对象，绝不写其它输出（防止污染协议）；
所有诊断信息走 stderr。

用法：
    python memory_cli.py
    echo '{"apiKey":"sk-xxx","baseUrl":"https://api.deepseek.com","model":"deepseek-v4-flash","messages":[{"role":"user","content":"我叫小明"}]}' | python memory_cli.py
"""

from __future__ import annotations

import json
import re
import sys

# 协议要求：stdin/stdout 统一按 UTF-8 收发（与 cli.py / tts_cli.py 一致）。
# Windows 下 Python 默认用系统代码页（GBK）读写文本流，不强制的话
# 返回 JSON 里的中文会被编成 GBK 字节，导致 C++ 侧 QJsonDocument 按 UTF-8 解析失败。
if hasattr(sys.stdin, "reconfigure"):
    try:
        sys.stdin.reconfigure(encoding="utf-8")
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError, OSError):
        pass


MAX_MSG_CHARS = 2000  # 单条消息送入 LLM 的最长字符数（超出截断，省 token）
DEFAULT_MAX_MESSAGES = 10


def _write_json(obj) -> None:
    sys.stdout.write(json.dumps(obj, ensure_ascii=False))
    sys.stdout.write("\n")
    sys.stdout.flush()


def chat_completions_url(base_url: str) -> str:
    """把设置页的 Base URL 规范成 chat/completions 端点（与 DeepSeekClient::chatCompletionsUrl 一致）。"""
    endpoint = base_url.strip()
    while endpoint.endswith("/"):
        endpoint = endpoint[:-1]
    if not endpoint.endswith("/chat/completions"):
        endpoint += "/chat/completions"
    return endpoint


def build_system_prompt(existing_facts: list) -> str:
    known = json.dumps(existing_facts, ensure_ascii=False) if existing_facts else "（无）"
    return (
        "你是一个记忆抽取助手。从用户与「企业」的对话中，抽取值得长期记住的事实。\n"
        "值得抽取的内容包括：\n"
        "- 用户的个人信息（名字、身份、喜好、习惯）\n"
        "- 用户与企业之间的约定、承诺、重要经历\n"
        "- 与后续对话有持续影响的关键事件\n\n"
        "不要抽取：临时情绪、客套话、与剧情推进无关的寒暄、一两次就过的一次性话题。\n\n"
        "以下是已知事实，如果新对话没有改变它们，就不要重复抽取：\n"
        f"{known}\n\n"
        "只输出一个 JSON 对象，不要输出任何其它内容，格式：\n"
        '{"facts": [{"key": "user_name", "value": "小明", "confidence": 0.9}]}\n'
        "- key：英文小写下划线，语义化，如 user_name、preference_tea、promise_sunset\n"
        "- value：简短中文事实陈述\n"
        "- confidence：0~1 之间的小数，表示对该事实的把握\n"
        "- 若没有值得记忆的新事实，输出 {\"facts\": []}"
    )


def format_messages(messages: list, max_messages: int) -> str:
    """把最近消息渲染成 user:/assistant: 文本，倒序截断到最多 max_messages 条。"""
    if not messages:
        return "（无对话内容）"
    recent = messages[-max_messages:]
    lines = []
    for m in recent:
        role = m.get("role", "?")
        content = str(m.get("content", "") or "")
        if len(content) > MAX_MSG_CHARS:
            content = content[:MAX_MSG_CHARS] + " …[truncated]"
        label = "用户" if role == "user" else ("企业" if role == "assistant" else role)
        lines.append(f"{label}: {content}")
    return "\n".join(lines)


def call_llm(api_key: str, base_url: str, model: str, system: str, user_content: str) -> dict:
    """POST 到 chat/completions，返回完整 JSON 响应。仅用标准库 urllib，无额外依赖。"""
    import urllib.request
    import urllib.error

    body = json.dumps({
        "model": model,
        "messages": [
            {"role": "system", "content": system},
            {"role": "user", "content": user_content},
        ],
        "temperature": 0.2,
        "stream": False,
    }, ensure_ascii=False).encode("utf-8")

    req = urllib.request.Request(
        chat_completions_url(base_url),
        data=body,
        method="POST",
        headers={
            "Content-Type": "application/json",
            "Authorization": "Bearer " + api_key,
        },
    )

    with urllib.request.urlopen(req, timeout=60) as resp:
        raw = resp.read()
    return json.loads(raw.decode("utf-8"))


def _extract_json_object(text: str):
    """从 LLM 输出中鲁棒地提取 JSON 对象。

    移植自 Soul-of-Waifu 的 soul_memory.py：
    - 去掉 ```json ... ``` 代码围栏
    - 用首个 { 到最后一个 } 做 brace-slice
    - 修复尾逗号
    - 多候选逐个尝试，全部失败返回 None
    """
    if not text:
        return None

    stripped = text.strip()
    if stripped.startswith("```"):
        stripped = re.sub(r'^```(json)?\s*|```$', "", stripped, flags=re.MULTILINE).strip()

    candidates = [stripped]

    first, last = stripped.find("{"), stripped.rfind("}")
    if first != -1 and last != -1 and last > first:
        candidates.append(stripped[first:last + 1])

    for candidate in list(candidates):
        repaired = re.sub(r",\s*([\]}])", r"\1", candidate)
        if repaired != candidate:
            candidates.append(repaired)

    for candidate in candidates:
        try:
            parsed = json.loads(candidate)
            if isinstance(parsed, dict):
                return parsed
        except Exception:
            continue

    return None


def normalize_facts(payload: dict) -> list:
    """从 LLM 返回的 JSON 里取出 facts 数组，规整成 [{key, value, confidence?}]。"""
    if not isinstance(payload, dict):
        return []
    facts = payload.get("facts", [])
    if not isinstance(facts, list):
        return []

    normalized = []
    for f in facts:
        if not isinstance(f, dict):
            continue
        key = str(f.get("key", "")).strip()
        value = str(f.get("value", "")).strip()
        if not key or not value:
            continue
        out = {"key": key, "value": value}
        conf = f.get("confidence")
        if isinstance(conf, (int, float)):
            out["confidence"] = conf
        normalized.append(out)
    return normalized


def main(argv=None) -> int:
    argv = argv if argv is not None else sys.argv

    raw = sys.stdin.read()
    if not raw.strip():
        _write_json({"ok": False, "content": "错误：空请求"})
        return 1

    try:
        req = json.loads(raw)
    except json.JSONDecodeError:
        _write_json({"ok": False, "content": "错误：请求不是合法 JSON"})
        return 1

    api_key = (req.get("apiKey") or "").strip()
    base_url = (req.get("baseUrl") or "").strip()
    model = (req.get("model") or "").strip()
    messages = req.get("messages") or []
    existing_facts = req.get("existingFacts") or []
    try:
        max_messages = int(req.get("maxMessages") or DEFAULT_MAX_MESSAGES)
    except (TypeError, ValueError):
        max_messages = DEFAULT_MAX_MESSAGES

    if not api_key:
        _write_json({"ok": False, "content": "错误：apiKey 为空"})
        return 1
    if not base_url:
        _write_json({"ok": False, "content": "错误：baseUrl 为空"})
        return 1
    if not model:
        _write_json({"ok": False, "content": "错误：model 为空"})
        return 1

    system = build_system_prompt(existing_facts)
    user_content = (
        "=== 最近对话 ===\n"
        + format_messages(messages, max_messages)
    )

    try:
        resp = call_llm(api_key, base_url, model, system, user_content)
    except Exception as exc:  # noqa: BLE001 —— 协议层兜底（网络错误/超时/HTTP 错误都在这）
        _write_json({"ok": False, "content": "错误：调用模型失败：%s" % exc})
        return 1

    # OpenAI 兼容响应：choices[0].message.content
    try:
        content = resp["choices"][0]["message"]["content"]
    except (KeyError, IndexError, TypeError):
        _write_json({"ok": False, "content": "错误：模型响应缺少 choices[0].message.content"})
        return 1

    payload = _extract_json_object(content)
    if payload is None:
        _write_json({"ok": False, "content": "错误：无法从模型输出中解析 JSON"})
        return 1

    facts = normalize_facts(payload)
    _write_json({"ok": True, "content": json.dumps(facts, ensure_ascii=False)})
    return 0


if __name__ == "__main__":
    sys.exit(main())
