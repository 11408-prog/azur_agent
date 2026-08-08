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
};

#endif // PROMPTLOADER_H
