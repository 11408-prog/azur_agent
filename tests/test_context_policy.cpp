#include <gtest/gtest.h>
#include <QJsonArray>
#include <QStringList>
#include "core/contextpolicy.h"

static QJsonObject makeMsg(const QString& role, const QString& content = "", const QString& toolCallId = "") {
    QJsonObject msg;
    msg["role"] = role;
    if (!content.isEmpty()) msg["content"] = content;
    if (!toolCallId.isEmpty()) msg["tool_call_id"] = toolCallId;
    return msg;
}

static QJsonObject makeToolResult(const QString &toolCallId, const QString &content) {
    return makeMsg("tool", content, toolCallId);
}

static QJsonObject makeAssistantWithToolCalls(const QStringList& toolIds,
                                               const QString &toolName = "read_file") {
    QJsonObject msg;
    msg["role"] = "assistant";
    QJsonArray calls;
    for (const QString& id : toolIds) {
        QJsonObject func;
        func["name"] = toolName;
        QJsonObject full;
        full["id"] = id;
        full["type"] = "function";
        full["function"] = func;
        calls.append(full);
    }
    msg["tool_calls"] = calls;
    return msg;
}

TEST(ContextPolicyTest, Trim_NoExceed_ReturnsSame) {
    QList<QJsonObject> msgs;
    msgs << makeMsg("system", "system prompt");
    msgs << makeMsg("user", "Hello");
    msgs << makeMsg("assistant", "Hi");
    msgs << makeMsg("tool", "result", "call_1");

    ContextReport report;
    auto result = ContextPolicy::trim(msgs, 100, 10, 999999, 10, &report);
    EXPECT_EQ(result.size(), msgs.size());
    EXPECT_EQ(report.originalCount, 4);
    EXPECT_EQ(report.trimmedCount, 4);
}

TEST(ContextPolicyTest, Trim_ProtectedTurns_KeepRecent) {
    QList<QJsonObject> msgs;
    msgs << makeMsg("system", "sys");
    for (int i = 0; i < 20; ++i) {
        msgs << makeMsg("user", QString("user %1").arg(i));
        msgs << makeMsg("assistant", QString("assistant %1").arg(i));
    }
    ContextReport report;
    auto result = ContextPolicy::trim(msgs, 10, 5, 999999, 5, &report);
    EXPECT_EQ(result.size(), 11);
    EXPECT_EQ(result.last()["content"].toString(), "assistant 19");
}

TEST(ContextPolicyTest, Trim_ToolCallIntegrity_KeepTogether) {
    QList<QJsonObject> msgs;
    msgs << makeMsg("system", "sys");
    msgs << makeAssistantWithToolCalls({"c1", "c2"});
    msgs << makeMsg("tool", "result1", "c1");
    msgs << makeMsg("tool", "result2", "c2");
    for (int i = 0; i < 30; ++i) {
        msgs << makeMsg("user", QString("junk %1").arg(i));
        msgs << makeMsg("assistant", "junk reply");
    }
    ContextReport report;
    auto result = ContextPolicy::trim(msgs, 10, 2, 999999, 0, &report);
    EXPECT_EQ(result.size(), 10);
    bool hasAss = false, hasTool1 = false, hasTool2 = false;
    for (const auto& m : result) {
        if (m["role"] == "assistant" && m.contains("tool_calls")) hasAss = true;
        if (m["role"] == "tool" && m["tool_call_id"] == "c1") hasTool1 = true;
        if (m["role"] == "tool" && m["tool_call_id"] == "c2") hasTool2 = true;
    }
    EXPECT_TRUE(hasAss);
    EXPECT_TRUE(hasTool1);
    EXPECT_TRUE(hasTool2);
}

// ---------------------------------------------------------------------
// 新增：总字符数上限（maxTotalSize）
// ---------------------------------------------------------------------

TEST(ContextPolicyTest, Trim_ExceedsTotalSize_ShrinksBelowLimit) {
    QList<QJsonObject> msgs;
    msgs << makeMsg("system", "sys");
    // 每条内容都很长，制造一个远超 maxTotalSize 的总量
    const QString bigContent = QString(2000, QChar('x'));
    for (int i = 0; i < 20; ++i) {
        msgs << makeMsg("user", QString("q%1 ").arg(i) + bigContent);
        msgs << makeMsg("assistant", QString("a%1 ").arg(i) + bigContent);
    }

    ContextReport report;
    // maxMessages 给得很宽松，真正起作用的应该是 maxTotalSize
    auto result = ContextPolicy::trim(msgs, 1000, 5, 5000, 2, &report);

    EXPECT_LT(report.trimmedSize, report.originalSize);
    EXPECT_GT(report.sizeSaved, 0);
    // system 消息必须始终保留
    bool hasSystem = false;
    for (const auto& m : result) {
        if (m["role"] == "system") hasSystem = true;
    }
    EXPECT_TRUE(hasSystem);
}

TEST(ContextPolicyTest, Trim_SystemMessage_NeverDropped) {
    QList<QJsonObject> msgs;
    msgs << makeMsg("system", "critical system prompt");
    for (int i = 0; i < 50; ++i) {
        msgs << makeMsg("user", QString("q%1").arg(i));
        msgs << makeMsg("assistant", QString("a%1").arg(i));
    }
    ContextReport report;
    // 极端严苛的上限，逼迫尽可能多地裁剪
    auto result = ContextPolicy::trim(msgs, 5, 0, 200, 0, &report);
    ASSERT_FALSE(result.isEmpty());
    EXPECT_EQ(result.first()["role"].toString(), "system");
}

// ---------------------------------------------------------------------
// maxToolResults 下限保护
//
// contextpolicy.cpp 里 droppableCount() 现在只统计角色为 tool 的候选，
// 不再混入旧对话候选。实际上，由于旧对话的 dropPriority 恒为 3+age（≥3），
// tool 消息的 dropPriority 恒为 0/1/2，候选按 dropPriority 降序处理时，
// 循环走到第一个 tool 候选之前必然已经清空了所有旧对话候选（要么全部丢完，
// 要么中途已经因为达标而 break，根本不会走到 tool）。所以这个统计口径的
// 调整在当前优先级方案下更多是"代码自证清晰 + 防止未来改动悄悄破坏下限"，
// 而不是修复一个当下就能观察到的行为差异——写测试时要避免依赖一个实际上
// 不会被触发的场景。
//
// 下面用一组完全没有旧对话候选（不含任何 user/assistant 纯文本消息，也不给
// assistant+tool_calls 配前置 user 轮次，让 assistant 因 currentTurn==0
// 被永久保护）的场景验证 maxToolResults 下限：assistant 恒被保护，阶段 3.5
// 会无条件把它名下的 tool 结果拉回来，所以预期是全部 tool 都存活——这不是
// 在测"能不能精确卡在 maxToolResults 条"，而是确认这类场景下裁剪逻辑不会
// 因为 droppableCount() 的统计对象变化而意外把 tool 结果丢过头、跌破下限。
// ---------------------------------------------------------------------

TEST(ContextPolicyTest, Trim_ToolResultsFloor_NeverDropsBelowMinimum) {
    QList<QJsonObject> msgs;
    msgs << makeMsg("system", "sys");
    const QStringList toolNames = {"read_file", "list_directory", "search_file_content",
                                    "read_file", "read_file"};
    for (int i = 0; i < toolNames.size(); ++i) {
        const QString id = QString("t%1").arg(i);
        msgs << makeAssistantWithToolCalls({id}, toolNames[i]);
        msgs << makeToolResult(id, QString("result %1 ").arg(i) + QString(500, QChar('x')));
    }

    ContextReport report;
    auto result = ContextPolicy::trim(msgs, 999999, 2, 100, 0, &report);

    int toolCount = 0;
    for (const auto& m : result) {
        if (m["role"] == "tool") ++toolCount;
    }
    EXPECT_GE(toolCount, 2);
}

// ---------------------------------------------------------------------
// 边界情况
// ---------------------------------------------------------------------

TEST(ContextPolicyTest, Trim_EmptyMessageList_ReturnsEmpty) {
    QList<QJsonObject> msgs;
    ContextReport report;
    auto result = ContextPolicy::trim(msgs, 80, 20, 100000, 15, &report);
    EXPECT_TRUE(result.isEmpty());
    EXPECT_EQ(report.originalCount, 0);
    EXPECT_EQ(report.trimmedCount, 0);
}

TEST(ContextPolicyTest, Trim_OnlySystemMessage_ReturnsUnchanged) {
    QList<QJsonObject> msgs;
    msgs << makeMsg("system", "sys only");
    ContextReport report;
    auto result = ContextPolicy::trim(msgs, 1, 0, 10, 0, &report);
    EXPECT_EQ(result.size(), 1);
    EXPECT_EQ(result.first()["role"].toString(), "system");
}

TEST(ContextPolicyTest, Trim_ZeroProtectedTurns_DoesNotCrash) {
    QList<QJsonObject> msgs;
    msgs << makeMsg("system", "sys");
    msgs << makeMsg("user", "hi");
    msgs << makeMsg("assistant", "hello");
    ContextReport report;
    EXPECT_NO_FATAL_FAILURE({
        ContextPolicy::trim(msgs, 80, 20, 100000, 0, &report);
    });
}

TEST(ContextPolicyTest, Trim_NullReport_DoesNotCrash) {
    QList<QJsonObject> msgs;
    msgs << makeMsg("system", "sys");
    msgs << makeMsg("user", "hi");
    EXPECT_NO_FATAL_FAILURE({
        ContextPolicy::trim(msgs, 80, 20, 100000, 15, nullptr);
    });
}

TEST(ContextPolicyTest, Trim_TrimmedCountNeverExceedsOriginal) {
    QList<QJsonObject> msgs;
    msgs << makeMsg("system", "sys");
    for (int i = 0; i < 15; ++i) {
        msgs << makeMsg("user", QString("q%1").arg(i));
        msgs << makeMsg("assistant", QString("a%1").arg(i));
    }
    ContextReport report;
    ContextPolicy::trim(msgs, 8, 2, 100000, 2, &report);
    EXPECT_LE(report.trimmedCount, report.originalCount);
    EXPECT_LE(report.trimmedSize, report.originalSize);
}
