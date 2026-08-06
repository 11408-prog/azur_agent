#include "contextpolicy.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <algorithm>

// ==================== ContextReport ====================

QString ContextReport::summary() const
{
    if (droppedTools == 0 && droppedUA == 0)
        return QStringLiteral("上下文: %1 条 / %2 KB (无需裁剪)")
            .arg(originalCount)
            .arg(originalSize / 1024.0, 0, 'f', 1);

    return QStringLiteral("上下文裁剪: %1 → %2 条 (丢弃 %3 tool + %4 对话, 节省 %5 KB)")
        .arg(originalCount)
        .arg(trimmedCount)
        .arg(droppedTools)
        .arg(droppedUA)
        .arg(sizeSaved / 1024.0, 0, 'f', 1);
}

// ==================== trim() 核心 ====================

QList<QJsonObject> ContextPolicy::trim(const QList<QJsonObject> &messages,
                                       int maxMessages,
                                       int maxToolResults,
                                       int maxTotalSize,
                                       int protectedTurns,
                                       ContextReport *report)
{
    const int n = messages.size();
    const int origSize = totalSize(messages);

    // 阶段 0：若未超出任何限制，直接返回
    if (n <= maxMessages && origSize <= maxTotalSize) {
        if (report) {
            ContextReport r;
            r.originalCount = n;
            r.trimmedCount  = n;
            r.originalSize  = origSize;
            r.trimmedSize   = origSize;
            // 仍然统计分类信息
            for (const QJsonObject &msg : messages) {
                const QString role = msg[QStringLiteral("role")].toString();
                if (role == QStringLiteral("system"))    ++r.systemCount;
                else if (role == QStringLiteral("user"))   ++r.userCount;
                else if (role == QStringLiteral("assistant")) ++r.assistantCount;
                else if (role == QStringLiteral("tool"))   ++r.toolCount;
            }
            *report = r;
        }
        return messages;
    }

    // ─── 阶段 1：全局优先级分类 ────────────────────────────
    //
    // dropPriority[i]:
    //   -1  = 永不丢弃（system / 保护轮次内的 user/assistant）
    //    0  = 高价值 tool（写操作 / 错误结果）
    //    1  = 中价值 tool（命令执行）
    //    2  = 低价值 tool（读操作）
    //    3  = 旧对话 user/assistant（超出保护轮次）
    //    4+ = 极旧对话（进一步区分年龄）
    //
    // msgOrder[i] = 全局序号（越小越旧），同 priority 内先丢旧的。

    QList<bool> keep(n, true);
    QList<int>  dropPriority(n, -1);
    QList<int>  msgOrder(n, -1);

    // 统计总轮次（以 user 消息计数）
    int totalTurns = 0;
    for (int i = 0; i < n; ++i) {
        if (messages[i][QStringLiteral("role")].toString() == QStringLiteral("user"))
            ++totalTurns;
    }

    int globalOrder = 0;

    {   // 第一遍扫描：为每个非 tool 消息分配优先级
        int currentTurn = 0;
        for (int i = 0; i < n; ++i) {
            const QJsonObject &msg = messages[i];
            const QString role = msg[QStringLiteral("role")].toString();

            if (role == QStringLiteral("system")) {
                // 系统消息永久保护
                continue;
            }

            if (role == QStringLiteral("user")) {
                ++currentTurn;
                int turnsFromEnd = totalTurns - currentTurn;
                if (turnsFromEnd < protectedTurns) {
                    continue;   // 受年龄保护
                }
                // 旧对话：轮次越早，优先级越高（越先丢弃）
                dropPriority[i] = 3 + (turnsFromEnd - protectedTurns);
                msgOrder[i] = globalOrder++;
                continue;
            }

            if (role == QStringLiteral("assistant")) {
                // assistant 与它前面的 user 同属一轮
                // 如果前面没有 user（不正常但防御性处理），也归为同一轮
                int turnsFromEnd = totalTurns - currentTurn;
                if (currentTurn == 0 || turnsFromEnd < protectedTurns) {
                    continue;   // 受年龄保护
                }
                dropPriority[i] = 3 + (turnsFromEnd - protectedTurns);
                msgOrder[i] = globalOrder++;
                continue;
            }
        }
    }

    {   // 第二遍扫描：为 tool 消息分配优先级
        for (int i = 0; i < n; ++i) {
            const QJsonObject &msg = messages[i];
            if (msg[QStringLiteral("role")].toString() != QStringLiteral("tool"))
                continue;

            const QString toolName = resolveToolName(messages, i);
            const QString content = msg[QStringLiteral("content")].toString();
            const bool isError = content.startsWith(QStringLiteral("Error:"), Qt::CaseInsensitive)
                              || content.startsWith(QStringLiteral("error:"), Qt::CaseInsensitive)
                              || content.startsWith(QStringLiteral("\u2717"));

            if (isError || isHighValue(toolName))
                dropPriority[i] = 0;   // 高价值
            else if (isLowValue(toolName))
                dropPriority[i] = 2;   // 低价值
            else
                dropPriority[i] = 1;   // 中价值

            msgOrder[i] = globalOrder++;
        }
    }

    // ─── 阶段 2：按总字符数裁剪 ────────────────────────────
    int currentSize = origSize;
    // 只统计还未丢弃、且角色是 tool 的候选数量。
    // 注意：这里之前统计的是所有还未丢弃的候选（旧对话 + tool 混算），
    // 导致下面 "至少保留 maxToolResults 条 tool" 的下限，只有在场上不再
    // 有可丢的旧对话候选、候选列表里几乎只剩 tool 消息时才会真正生效——
    // 旧对话候选没清空之前，这个判断形同虚设。现在改成只数 tool，
    // 让这个下限在任意裁剪阶段都准确成立。
    auto droppableCount = [&]() {
        int c = 0;
        for (int i = 0; i < n; ++i)
            if (keep[i] && dropPriority[i] >= 0
                && messages[i][QStringLiteral("role")].toString() == QStringLiteral("tool"))
                ++c;
        return c;
    };

    if (currentSize > maxTotalSize) {
        QList<int> candidates;
        for (int i = 0; i < n; ++i)
            if (dropPriority[i] >= 0)
                candidates.append(i);

        // 排序：高 priority（先丢）→ 同 priority 内旧（小 order）先丢
        std::sort(candidates.begin(), candidates.end(),
                  [&](int a, int b) {
                      if (dropPriority[a] != dropPriority[b])
                          return dropPriority[a] > dropPriority[b];
                      return msgOrder[a] < msgOrder[b];
                  });

        for (int idx : candidates) {
            if (currentSize <= maxTotalSize) break;
            // tool 类至少保留 maxToolResults 条
            if (messages[idx][QStringLiteral("role")].toString() == QStringLiteral("tool")
                && droppableCount() <= maxToolResults)
                break;

            currentSize -= messageSize(messages[idx]);
            keep[idx] = false;
        }
    }

    // ─── 阶段 3：按消息数量上限裁剪 ────────────────────────
    int keptCount = 0;
    for (int i = 0; i < n; ++i)
        if (keep[i]) ++keptCount;

    if (keptCount > maxMessages) {
        int toDrop = keptCount - maxMessages;

        QList<int> candidates;
        for (int i = 0; i < n; ++i)
            if (keep[i] && dropPriority[i] >= 0)
                candidates.append(i);

        std::sort(candidates.begin(), candidates.end(),
                  [&](int a, int b) {
                      if (dropPriority[a] != dropPriority[b])
                          return dropPriority[a] > dropPriority[b];
                      return msgOrder[a] < msgOrder[b];
                  });

        for (int idx : candidates) {
            if (toDrop <= 0) break;
            // tool 类至少保留 maxToolResults 条
            if (messages[idx][QStringLiteral("role")].toString() == QStringLiteral("tool")
                && droppableCount() <= maxToolResults)
                break;

            keep[idx] = false;
            --toDrop;
        }
    }

    // ─── 阶段 3.5：tool call 完整性保护 ──────────────────
    // 如果 assistant（含 tool_calls）被保留，则它所有的 tool 结果也必须保留，
    // 否则 API 会拒绝请求（"insufficient tool messages"）。
    // 反向约束：如果 assistant 被丢弃，它后面的 tool 也应当一并丢弃（无主 tool）。
    for (int i = 0; i < n; ++i) {
        if (messages[i][QStringLiteral("role")].toString() != QStringLiteral("assistant"))
            continue;
        if (!messages[i].contains(QStringLiteral("tool_calls")))
            continue;

        bool assistantKept = keep[i];
        // 收集此 assistant 的所有 tool 消息（连续跟在后面的 tool）
        int groupBegin = -1, groupEnd = -1;
        for (int j = i + 1; j < n; ++j) {
            if (messages[j][QStringLiteral("role")].toString() == QStringLiteral("tool")) {
                if (groupBegin < 0) groupBegin = j;
                groupEnd = j;
            } else {
                break;
            }
        }

        if (groupBegin < 0)
            continue;   // 没有 tool 结果（正常，可能是纯文本+tool_calls 的过渡消息）

        if (assistantKept) {
            // assistant 保留 → 所有 tool 结果也必须保留
            for (int j = groupBegin; j <= groupEnd; ++j)
                keep[j] = true;
        } else {
            // assistant 被丢弃 → 对应的 tool 结果也丢弃（避免无主 tool）
            for (int j = groupBegin; j <= groupEnd; ++j)
                keep[j] = false;
        }
    }

    // ─── 阶段 4：组装结果 & 报告 ──────────────────────────
    QList<QJsonObject> result;
    result.reserve(n);
    for (int i = 0; i < n; ++i) {
        if (keep[i])
            result.append(messages[i]);
    }

    if (report) {
        ContextReport r;
        r.originalCount = n;
        r.trimmedCount  = result.size();
        r.originalSize  = origSize;
        r.trimmedSize   = totalSize(result);

        for (int i = 0; i < n; ++i) {
            const QString role = messages[i][QStringLiteral("role")].toString();
            if (role == QStringLiteral("system"))          ++r.systemCount;
            else if (role == QStringLiteral("user"))        ++r.userCount;
            else if (role == QStringLiteral("assistant"))   ++r.assistantCount;
            else if (role == QStringLiteral("tool")) {
                ++r.toolCount;
                // 统计 tool 价值分布
                if (dropPriority[i] == 0)   ++r.highValueTools;
                else if (dropPriority[i] == 2) ++r.lowValueTools;
                else if (dropPriority[i] == 1) ++r.midValueTools;
            }

            if (!keep[i]) {
                if (role == QStringLiteral("tool"))
                    ++r.droppedTools;
                else if (role == QStringLiteral("user") || role == QStringLiteral("assistant"))
                    ++r.droppedUA;
                r.sizeSaved += messageSize(messages[i]);
            }
        }
        *report = r;
    }

    return result;
}


// ==================== 工具函数 ====================

int ContextPolicy::messageSize(const QJsonObject &msg)
{
    return QJsonDocument(msg).toJson(QJsonDocument::Compact).size();
}

int ContextPolicy::totalSize(const QList<QJsonObject> &messages)
{
    int size = 0;
    for (const QJsonObject &msg : messages)
        size += messageSize(msg);
    return size;
}

QString ContextPolicy::resolveToolName(const QList<QJsonObject> &messages, int toolMsgIndex)
{
    const QString toolCallId = messages[toolMsgIndex][QStringLiteral("tool_call_id")].toString();
    if (toolCallId.isEmpty())
        return {};

    for (int i = toolMsgIndex - 1; i >= 0; --i) {
        if (messages[i][QStringLiteral("role")].toString() == QStringLiteral("assistant")
            && messages[i].contains(QStringLiteral("tool_calls"))) {
            const QJsonArray toolCalls = messages[i][QStringLiteral("tool_calls")].toArray();
            for (const QJsonValue &v : toolCalls) {
                const QJsonObject tc = v.toObject();
                if (tc[QStringLiteral("id")].toString() == toolCallId)
                    return tc[QStringLiteral("function")].toObject()[QStringLiteral("name")].toString();
            }
            return {};
        }
    }
    return {};
}

bool ContextPolicy::isHighValue(const QString &toolName)
{
    return toolName == QStringLiteral("write_file")
        || toolName == QStringLiteral("apply_patch")
        || toolName == QStringLiteral("create_file")
        || toolName == QStringLiteral("delete_file")
        || toolName == QStringLiteral("rename_file")
        || toolName == QStringLiteral("git_commit");
}

bool ContextPolicy::isLowValue(const QString &toolName)
{
    return toolName == QStringLiteral("read_file")
        || toolName == QStringLiteral("list_directory")
        || toolName == QStringLiteral("search_code")
        || toolName == QStringLiteral("read_image")
        || toolName == QStringLiteral("glob");
}
