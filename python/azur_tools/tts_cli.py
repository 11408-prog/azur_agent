"""edge-tts 语音合成 CLI —— stdin/stdout JSON 协议，与 cli.py 同款约定。

请求（stdin 一行 JSON）：
    {"text": "...", "voice": "zh-CN-XiaoyiNeural", "output": "abs/path.mp3"}

响应（stdout 一行 JSON，保证可被 json.loads 解析）：
    成功 {"ok": true, "content": "<output 路径>"}
    失败 {"ok": false, "content": "<错误描述>"}

stdout 只写这一个 JSON 对象，绝不写其它输出（防止污染协议）；
所有诊断信息走 stderr。

用法：
    python tts_cli.py
    echo '{"text":"你好","voice":"zh-CN-XiaoyiNeural","output":"C:/tmp/a.mp3"}' | python tts_cli.py
"""

from __future__ import annotations

import json
import sys

# 协议要求：stdin/stdout 统一按 UTF-8 收发（与 cli.py 一致）。
# Windows 下 Python 默认用系统代码页（GBK）读写文本流，不强制的话
# 返回 JSON 里的中文会被编成 GBK 字节，导致 C++ 侧 QJsonDocument
# 按 UTF-8 解析失败。
if hasattr(sys.stdin, "reconfigure"):
    try:
        sys.stdin.reconfigure(encoding="utf-8")
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError, OSError):
        pass


def _write_json(obj) -> None:
    sys.stdout.write(json.dumps(obj, ensure_ascii=False))
    sys.stdout.write("\n")
    sys.stdout.flush()


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

    text = (req.get("text") or "").strip()
    voice = req.get("voice") or "zh-CN-XiaoyiNeural"
    output = (req.get("output") or "").strip()

    if not text:
        _write_json({"ok": False, "content": "错误：text 为空"})
        return 1
    if not output:
        _write_json({"ok": False, "content": "错误：output 路径为空"})
        return 1

    try:
        import asyncio
        import edge_tts

        asyncio.run(edge_tts.Communicate(text, voice).save(output))
    except Exception as exc:  # noqa: BLE001 —— 协议层兜底，任何异常都转成 JSON 错误
        _write_json({"ok": False, "content": "错误：%s" % exc})
        return 1

    _write_json({"ok": True, "content": output})
    return 0


if __name__ == "__main__":
    sys.exit(main())
