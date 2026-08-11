"""Azur Agent AI 评测（T3）—— 评估记忆抽取与语气一致性两项核心能力。

纯标准库，mock 模式离线可跑；真实模式复用 memory_cli 的 HTTP 实现。

模块结构：
    scenarios_memory.json  记忆抽取场景（对话 → 期望抽取的事实）
    scenarios_tone.json    语气一致性场景（prompt → 期望的回复姿态）
    eval_runner.py         评测管线：场景加载 / 双后端 / judge / 报告

用法见 eval_runner.py 顶部 docstring。
"""
