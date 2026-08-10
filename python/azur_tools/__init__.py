"""azur_tools — 与 C++ AgentEngine 工具执行层对标的独立 Python 实现。

纯标准库实现，零第三方依赖。通过 stdin/stdout 的 JSON 协议与宿主进程
（如 C++ 侧的 QProcess）交互，也可作为普通模块直接 import。

模块结构：
    executor.py  核心工具执行逻辑（read_file / list_directory + 路径安全）
    cli.py       stdin/stdout JSON 协议入口（C++ 集成的缝）
    demo.py      演示/测试程序，IDE 或终端直接运行查看效果
"""
__version__ = "0.1.0"
