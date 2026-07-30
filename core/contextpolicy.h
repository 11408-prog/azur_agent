#ifndef CONTEXTPOLICY_H
#define CONTEXTPOLICY_H

#include <QList>
#include <QJsonObject>
#include <QString>

// ContextReport：一次 trim() 调用的诊断报告。
// 由 trim() 通过出参返回，供 UI / 日志展示。
struct ContextReport
{
    int originalCount  = 0;     // 原始消息数
    int trimmedCount   = 0;     // 裁剪后的消息数
    int originalSize   = 0;     // 原始总字符数
    int trimmedSize    = 0;     // 裁剪后的总字符数

    int systemCount    = 0;     // system 消息数
    int userCount      = 0;     // user 消息数
    int assistantCount = 0;     // assistant 消息数
    int toolCount      = 0;     // tool 消息数

    int highValueTools = 0;     // 高价值 tool（写操作/错误）
    int midValueTools  = 0;     // 中价值 tool（命令）
    int lowValueTools  = 0;     // 低价值 tool（读操作）

    int droppedTools     = 0;   // 丢弃的 tool 数
    int droppedUA        = 0;   // 丢弃的 user/assistant 数
    int sizeSaved        = 0;   // 节省的字符数

    // 简短的诊断文本（供日志 / 状态栏使用）
    QString summary() const;
};

// ContextPolicy：轻量级上下文裁剪策略。
//
// 纯策略类，不维护任何状态。输入 AgentEngine 的 messageHistory_，
// 输出裁剪后的副本，由 AgentEngine 决定是否替换原历史或仅用于 API 请求。
//
// 裁剪策略：
//   1. system 消息永久保护，永不丢弃
//   2. 最近的 N 轮对话（user/assistant）受年龄保护
//   3. 超出保护轮次的旧对话按年龄降权，优先于 tool 消息丢弃
//   4. tool 消息根据工具价值分级：
//      - 高价值：写操作（write_file, apply_patch 等）及错误结果
//      - 中价值：命令执行（run_command 等）
//      - 低价值：读操作（read_file, list_directory 等）——优先丢弃
//   5. 同时支持消息数量上限和总字符数上限
//   6. 确保至少保留 maxToolResults 条工具结果
class ContextPolicy
{
public:
    // 裁剪消息历史，返回适合发送给 API 的消息列表。
    // maxMessages:      硬上限，超过此数量则裁剪
    // maxToolResults:  至少保留的工具结果数量
    // maxTotalSize:    总字符数上限（所有消息 JSON 紧凑格式的字符数之和）
    // protectedTurns:  保留的最新对话轮次数（每轮 = 一条 user + 对应 assistant）
    //                  超出此轮次的旧 user/assistant 变为可丢弃
    // report:          非空则输出本次裁剪的诊断报告
    static QList<QJsonObject> trim(const QList<QJsonObject> &messages,
                                   int maxMessages = 80,
                                   int maxToolResults = 20,
                                   int maxTotalSize = 100000,
                                   int protectedTurns = 15,
                                   ContextReport *report = nullptr);

private:
    static int messageSize(const QJsonObject &msg);
    static int totalSize(const QList<QJsonObject> &messages);
    static QString resolveToolName(const QList<QJsonObject> &messages, int toolMsgIndex);
    static bool isHighValue(const QString &toolName);
    static bool isLowValue(const QString &toolName);
};

#endif // CONTEXTPOLICY_H
