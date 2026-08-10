#ifndef PROMPTLOADER_H
#define PROMPTLOADER_H

#include <QString>

class PromptLoader
{
public:
    static QString loadFile(const QString &filename);

    // 完整版人格：人格描述 + 台词参考，两个文件拼接。
    static QString buildFullPersonaPrompt();

    // 构建最终 system prompt。
    // style: 0 = 精简人格（lite/enterprise.md，本地小模型推荐），
    //        1 = 完整人格（full/ 下的人格 + 台词参考，云端大模型推荐）。
    static QString buildChatSystemPrompt(int style);

    // 构建"历史之后"的语气约束指令（Character Card V2 的 post_history_instructions
    // 思路）：离生成点最近的一段 system 指令，用于防止对话变长后人设漂移。
    // 与 buildChatSystemPrompt 分开，因为二者角色不同——一个是开头的 system
    // prompt，一个是历史之后的 system 消息。
    // style: 0 = 精简（lite/enterprise_instructions.md），
    //        1 = 完整（full/enterprise_instructions.md）。
    static QString buildPostHistoryInstructions(int style);
};

#endif // PROMPTLOADER_H
