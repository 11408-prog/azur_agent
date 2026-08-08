#include <gtest/gtest.h>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include "core/agent_engine.h"
#include "core/ai_client.h"
#include "core/tool_executor.h"

// ---------------------------------------------------------------------
// FakeDeepSeekClient —— 不发真实网络请求，只记录调用参数。
//
// AgentEngine 通过连接 client_ 的信号驱动整个循环，测试时手动 emit 这些
// 信号（它们本质上是 public 成员函数，Qt 里 signals: 就是 public:）就能
// 精确模拟"AI 流式返回了什么"这种没法在单元测试里真实构造的场景。
// ---------------------------------------------------------------------

class FakeDeepSeekClient : public DeepSeekClient
{
public:
    struct SendCall {
        QString apiKey;
        QString baseUrl;
        QString model;
        QJsonArray messages;
        QJsonArray tools;
    };
    QList<SendCall> sendCalls;
    int cancelCallCount = 0;

    void sendMessage(const QString &apiKey, const QString &baseUrl, const QString &model,
                      const QJsonArray &messages, const QJsonArray &tools) override
    {
        sendCalls.append(SendCall{apiKey, baseUrl, model, messages, tools});
    }

    void cancel() override
    {
        ++cancelCallCount;
    }
};

class AgentEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        fakeClient = new FakeDeepSeekClient();
        engine = new AgentEngine(fakeClient);
        tempDir = new QTemporaryDir();
        ASSERT_TRUE(tempDir->isValid());
        QFile f(tempDir->path() + "/hello.txt");
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("hello world");
        f.close();
    }

    void TearDown() override {
        delete engine;
        delete fakeClient;
        delete tempDir;
        ToolExecutor::setAllowedPaths({});
    }

    static QList<QJsonObject> userHistory(const QString &text) {
        QJsonObject msg;
        msg["role"] = "user";
        msg["content"] = text;
        return {msg};
    }

    // 构造一个最小的 read_file 工具调用 JSON，模拟模型请求读取某个文件
    static QJsonArray makeReadFileToolCall(const QString &id, const QString &relPath) {
        QJsonObject func;
        func["name"] = "read_file";
        func["arguments"] = QString("{\"path\":\"%1\"}").arg(relPath);
        QJsonObject tc;
        tc["id"] = id;
        tc["type"] = "function";
        tc["function"] = func;
        return QJsonArray{tc};
    }

    FakeDeepSeekClient *fakeClient;
    AgentEngine *engine;
    QTemporaryDir *tempDir;
};

// ---------------------------------------------------------------------
// isBusy() / start() 基本状态流转
// ---------------------------------------------------------------------

TEST_F(AgentEngineTest, IsBusy_FalseBeforeStart) {
    EXPECT_FALSE(engine->isBusy());
}

TEST_F(AgentEngineTest, Start_SetsBusyTrue_AndCallsSendMessageOnce) {
    engine->start("sk-test", "https://api.deepseek.com", "deepseek-chat",
                  userHistory("你好"), "你是企业", QJsonArray(), "");

    EXPECT_TRUE(engine->isBusy());
    ASSERT_EQ(fakeClient->sendCalls.size(), 1);
    EXPECT_EQ(fakeClient->sendCalls[0].apiKey, "sk-test");
    EXPECT_EQ(fakeClient->sendCalls[0].model, "deepseek-chat");
}

// ---------------------------------------------------------------------
// system prompt 注入策略：云端模型走 role=system，本地模型拼进第一条 user 消息
// ---------------------------------------------------------------------

TEST_F(AgentEngineTest, Start_CloudModel_UsesSystemRoleMessage) {
    engine->start("key", "https://api.deepseek.com", "model",
                  userHistory("你好"), "系统提示词", QJsonArray(), "");

    ASSERT_EQ(fakeClient->sendCalls.size(), 1);
    QJsonArray messages = fakeClient->sendCalls[0].messages;
    ASSERT_GE(messages.size(), 1);
    QJsonObject first = messages.first().toObject();
    EXPECT_EQ(first["role"].toString(), "system");
    EXPECT_EQ(first["content"].toString(), "系统提示词");
}

TEST_F(AgentEngineTest, Start_LocalModelBaseUrl_InjectsSystemPromptIntoFirstUserMessage) {
    engine->start("key", "http://localhost:11434", "llama3",
                  userHistory("你好"), "系统提示词", QJsonArray(), "");

    ASSERT_EQ(fakeClient->sendCalls.size(), 1);
    QJsonArray messages = fakeClient->sendCalls[0].messages;

    // 本地模型对 role=system 遵循度差，不应该出现 system 消息
    for (const QJsonValue &v : messages) {
        EXPECT_NE(v.toObject()["role"].toString(), "system");
    }

    ASSERT_GE(messages.size(), 1);
    QString firstContent = messages.first().toObject()["content"].toString();
    EXPECT_TRUE(firstContent.contains("系统提示词"));
    EXPECT_TRUE(firstContent.contains("你好"));
}

// ---------------------------------------------------------------------
// 信号转发：client_ 的信号应该原样（或加工后）转发成 AgentEngine 自己的信号
// ---------------------------------------------------------------------

TEST_F(AgentEngineTest, ChunkReceived_RelaysFromClient) {
    engine->start("key", "https://api.deepseek.com", "model",
                  userHistory("你好"), "", QJsonArray(), "");

    QSignalSpy spy(engine, &AgentEngine::chunkReceived);
    emit fakeClient->chunkReceived("片段1");
    emit fakeClient->chunkReceived("片段2");

    ASSERT_EQ(spy.count(), 2);
    EXPECT_EQ(spy.at(0).at(0).toString(), "片段1");
    EXPECT_EQ(spy.at(1).at(0).toString(), "片段2");
}

TEST_F(AgentEngineTest, ResponseCompleted_EmitsFinished_AndStopsRunning) {
    engine->start("key", "https://api.deepseek.com", "model",
                  userHistory("你好"), "", QJsonArray(), "");

    QSignalSpy spy(engine, &AgentEngine::finished);
    emit fakeClient->responseCompleted("你好呀，有什么可以帮你的");

    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toString(), "你好呀，有什么可以帮你的");
    EXPECT_FALSE(engine->isBusy());
}

TEST_F(AgentEngineTest, ResponseCompleted_AppendsAssistantMessageToHistory) {
    engine->start("key", "https://api.deepseek.com", "model",
                  userHistory("你好"), "", QJsonArray(), "");
    emit fakeClient->responseCompleted("你好呀");

    QList<QJsonObject> history = engine->messageHistory();
    ASSERT_EQ(history.size(), 2); // 原来的 user + 新增的 assistant
    EXPECT_EQ(history.last()["role"].toString(), "assistant");
    EXPECT_EQ(history.last()["content"].toString(), "你好呀");
}

TEST_F(AgentEngineTest, ErrorOccurred_RelaysFromClient_AndStopsRunning) {
    engine->start("key", "https://api.deepseek.com", "model",
                  userHistory("你好"), "", QJsonArray(), "");

    QSignalSpy spy(engine, &AgentEngine::errorOccurred);
    emit fakeClient->errorOccurred("网络错误");

    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toString(), "网络错误");
    EXPECT_FALSE(engine->isBusy());
}

// ---------------------------------------------------------------------
// cancel()
// ---------------------------------------------------------------------

TEST_F(AgentEngineTest, Cancel_WhileRunning_CallsClientCancel_AndStopsRunning) {
    engine->start("key", "https://api.deepseek.com", "model",
                  userHistory("你好"), "", QJsonArray(), "");
    ASSERT_TRUE(engine->isBusy());

    engine->cancel();

    EXPECT_FALSE(engine->isBusy());
    EXPECT_EQ(fakeClient->cancelCallCount, 1);
}

TEST_F(AgentEngineTest, Cancel_WhenNotRunning_DoesNotCallClientCancel) {
    engine->cancel();
    EXPECT_EQ(fakeClient->cancelCallCount, 0);
}

// ---------------------------------------------------------------------
// 工具调用循环：这是 AgentEngine 最核心也最容易出问题的一块——
// 收到工具调用 -> 真的执行 ToolExecutor -> 结果写回历史 -> 发起下一轮请求
// ---------------------------------------------------------------------

TEST_F(AgentEngineTest, ToolCallRound_ExecutesReadFile_AppendsResultAndSendsFollowUpRequest) {
    engine->start("key", "https://api.deepseek.com", "model",
                  userHistory("读一下 hello.txt"), "",
                  ToolExecutor::toolDefinitions(), tempDir->path());
    ASSERT_EQ(fakeClient->sendCalls.size(), 1);

    emit fakeClient->toolCallsReceived(makeReadFileToolCall("call_1", "hello.txt"));

    // 工具执行完之后应该自动发起第二轮请求，引擎仍处于占用中
    ASSERT_EQ(fakeClient->sendCalls.size(), 2);
    EXPECT_TRUE(engine->isBusy());

    QList<QJsonObject> history = engine->messageHistory();
    // [0]=原始 user, [1]=assistant(带 tool_calls), [2]=tool 结果
    ASSERT_EQ(history.size(), 3);
    EXPECT_EQ(history[1]["role"].toString(), "assistant");
    EXPECT_TRUE(history[1].contains("tool_calls"));
    EXPECT_EQ(history[2]["role"].toString(), "tool");
    EXPECT_EQ(history[2]["tool_call_id"].toString(), "call_1");
    EXPECT_TRUE(history[2]["content"].toString().contains("hello world"));
}

TEST_F(AgentEngineTest, ToolCallRound_NonExistentFile_ToolResultContainsError) {
    engine->start("key", "https://api.deepseek.com", "model",
                  userHistory("读一下不存在的文件"), "",
                  ToolExecutor::toolDefinitions(), tempDir->path());

    emit fakeClient->toolCallsReceived(makeReadFileToolCall("call_1", "does_not_exist.txt"));

    QList<QJsonObject> history = engine->messageHistory();
    ASSERT_EQ(history.size(), 3);
    // 工具执行失败不应该让 AgentEngine 崩溃或者提前结束，而是把错误信息
    // 当作 tool 结果正常写回历史，交给模型自己决定怎么应对
    EXPECT_EQ(history[2]["role"].toString(), "tool");
    EXPECT_TRUE(history[2]["content"].toString().contains("错误"));
    EXPECT_TRUE(engine->isBusy()); // 仍然会发起下一轮请求，不是直接失败终止
}

TEST_F(AgentEngineTest, MaxToolRounds_StopsWithErrorAfterLimitExceeded) {
    engine->setMaxToolRounds(2);
    engine->start("key", "https://api.deepseek.com", "model",
                  userHistory("反复调用工具"), "",
                  ToolExecutor::toolDefinitions(), tempDir->path());

    QSignalSpy errorSpy(engine, &AgentEngine::errorOccurred);
    QJsonArray toolCall = makeReadFileToolCall("call_x", "hello.txt");

    emit fakeClient->toolCallsReceived(toolCall); // 第1轮，未超限
    EXPECT_EQ(errorSpy.count(), 0);
    emit fakeClient->toolCallsReceived(toolCall); // 第2轮，未超限（等于上限）
    EXPECT_EQ(errorSpy.count(), 0);
    emit fakeClient->toolCallsReceived(toolCall); // 第3轮，超过上限，应该被拦停

    ASSERT_EQ(errorSpy.count(), 1);
    EXPECT_TRUE(errorSpy.at(0).at(0).toString().contains("轮次过多"));
    EXPECT_FALSE(engine->isBusy());
}

// ---------------------------------------------------------------------
// messageHistory() 的可观察性
// ---------------------------------------------------------------------

TEST_F(AgentEngineTest, MessageHistory_InitiallyContainsOnlyProvidedHistory) {
    engine->start("key", "https://api.deepseek.com", "model",
                  userHistory("你好"), "", QJsonArray(), "");

    QList<QJsonObject> history = engine->messageHistory();
    ASSERT_EQ(history.size(), 1);
    EXPECT_EQ(history[0]["role"].toString(), "user");
    EXPECT_EQ(history[0]["content"].toString(), "你好");
}
