#ifndef PROMPTLOADER_H
#define PROMPTLOADER_H

#include <QString>

class PromptLoader
{
public:
    static QString loadFile(const QString &filename);

    // 项目模式：完整人格 + 台词参考 + Agent 工具准则（要调用工具，需要完整指令）
    static QString buildSystemPrompt();

    // 聊天模式：只加载人格（不含 agent_core.md 工具准则）。
    // style: 0 = 精简人格（本地小模型），1 = 完整人格（云端大模型）。
    // 聊天不需要工具调用，加载 agent_core 只会让模型（尤其是小模型）困惑。
    static QString buildChatSystemPrompt(int style);
};

#endif // PROMPTLOADER_H
