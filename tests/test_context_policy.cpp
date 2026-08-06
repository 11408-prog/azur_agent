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

static QJsonObject makeAssistantWithToolCalls(const QStringList& toolIds) {
    QJsonObject msg;
    msg["role"] = "assistant";
    QJsonArray calls;
    for (const QString& id : toolIds) {
        QJsonObject func;
        func["name"] = "read_file";
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
