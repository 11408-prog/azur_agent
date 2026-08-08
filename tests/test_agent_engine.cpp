#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <QCoreApplication>
#include <QObject>

#include "core/agent_engine.h"
#include "core/ai_client.h"
#include "core/tool_executor.h"

using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::AnyNumber;

// ============ Mock DeepSeekClient ============
class MockDeepSeekClient : public DeepSeekClient {
public:
    MockDeepSeekClient(QObject* parent = nullptr) : DeepSeekClient(parent) {}

    MOCK_METHOD(void, sendMessage,
                (const QString&, const QString&, const QString&,
                 const QJsonArray&, const QJsonArray&), (override));
    MOCK_METHOD(void, cancel, (), (override));
    // 如果基类没有 isRunning，注释掉或删除
    // MOCK_METHOD(bool, isRunning, (), (const, override));

    void emitChunk(const QString& delta) { emit chunkReceived(delta); }
    void emitResponseCompleted(const QString& text) { emit responseCompleted(text); }
    void emitToolCalls(const QJsonArray& calls) { emit toolCallsReceived(calls); }
    void emitError(const QString& msg) { emit errorOccurred(msg); }
};

// ============ 测试夹具 ============
class AgentEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        mockClient = new MockDeepSeekClient;
        engine = new AgentEngine(mockClient);
        apiKey = "test-key";
        baseUrl = "https://api.deepseek.com";
        model = "deepseek-chat";
        systemPrompt = "You are Enterprise.";
        workspace = "/tmp/test_workspace";
    }
    void TearDown() override {
        engine->deleteLater();
        // mockClient 由 engine 析构时删除，不要手动 delete
    }
    MockDeepSeekClient* mockClient;
    AgentEngine* engine;
    QString apiKey, baseUrl, model, systemPrompt, workspace;
};

// ============ 测试用例（示例） ============
TEST_F(AgentEngineTest, Start_SendsRequestWithCorrectParams) {
    QList<QJsonObject> history;
    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = "Hello";
    history << userMsg;

    QJsonArray tools = ToolExecutor::toolDefinitions();

    EXPECT_CALL(*mockClient, sendMessage(apiKey, baseUrl, model, _, tools)).Times(1);

    engine->start(apiKey, baseUrl, model, history, systemPrompt, tools, workspace);
    EXPECT_TRUE(engine->isRunning());
}


TEST_F(AgentEngineTest, Start_LocalModel_InjectSystemIntoFirstUser) {
    QString localBaseUrl = "http://localhost:11434";  // Ollama
    QList<QJsonObject> history;
    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = "Hello";
    history << userMsg;
    
    // 捕获最终发送的消息
    QJsonArray capturedMessages;
    EXPECT_CALL(*mockClient, sendMessage(
        testing::_, localBaseUrl, testing::_,
        testing::_, testing::_
    )).WillOnce(testing::Invoke([&](auto, auto, auto, const QJsonArray& msgs, auto) {
        capturedMessages = msgs;
    }));
    
    engine->start(apiKey, localBaseUrl, model, history, systemPrompt, {}, workspace);
    
    // 验证：没有 system 角色，第一个 user 消息包含了 systemPrompt
    ASSERT_GT(capturedMessages.size(), 0);
    QJsonObject firstMsg = capturedMessages[0].toObject();
    EXPECT_EQ(firstMsg["role"].toString(), "user");
    EXPECT_TRUE(firstMsg["content"].toString().contains("You are Enterprise."));
}

TEST_F(AgentEngineTest, OnToolCallsReceived_ExecutesAndContinues) {
    // 准备工具调用响应
    QJsonArray toolCalls;
    QJsonObject tc;
    tc["id"] = "call_1";
    tc["type"] = "function";
    QJsonObject func;
    func["name"] = "list_directory";
    func["arguments"] = R"({"path":"."})";
    tc["function"] = func;
    toolCalls.append(tc);
    
    // 模拟 client 发出工具调用
    QList<QJsonObject> history;
    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = "List files";
    history << userMsg;
    
    // 设置工作区真实存在
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    
    // 期望：工具执行后，会再次调用 sendRequest（继续对话）
    EXPECT_CALL(*mockClient, sendMessage(_, _, _, _, _)).Times(1);
    
    engine->start(apiKey, baseUrl, model, history, systemPrompt, 
                  ToolExecutor::toolDefinitions(), tempDir.path());
    
    // 手动触发工具调用信号
    mockClient->emitToolCalls(toolCalls);
    
    // 等待 Qt 事件循环处理
    QCoreApplication::processEvents();
}

TEST_F(AgentEngineTest, Cancel_StopsRunningAndDisconnects) {
    QList<QJsonObject> history;
    EXPECT_CALL(*mockClient, sendMessage(_, _, _, _, _)).Times(1);
    EXPECT_CALL(*mockClient, cancel()).Times(1);
    
    engine->start(apiKey, baseUrl, model, history, systemPrompt, {}, workspace);
    engine->cancel();
    EXPECT_FALSE(engine->isRunning());
}

TEST_F(AgentEngineTest, MaxToolRounds_Exceeded_EmitsError) {
    // 设置最大轮次为 1
    engine->setMaxToolRounds(1);
    
    QList<QJsonObject> history;
    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = "Do something";
    history << userMsg;
    
    QJsonArray toolCalls;
    QJsonObject tc;
    tc["id"] = "call_1";
    tc["type"] = "function";
    QJsonObject func;
    func["name"] = "list_directory";
    func["arguments"] = R"({"path":"."})";
    tc["function"] = func;
    toolCalls.append(tc);
    
    bool errorEmitted = false;
    QObject::connect(engine, &AgentEngine::errorOccurred,
                     [&](const QString& msg) { /* ... */ });
    
    engine->start(apiKey, baseUrl, model, history, systemPrompt, 
                  ToolExecutor::toolDefinitions(), "/tmp");
    
    // 触发第一次工具调用 → 工具执行 → sendRequest → toolRound_=1
    mockClient->emitToolCalls(toolCalls);
    QCoreApplication::processEvents();
    
    // 模拟第二次工具调用 → 触发轮次上限检测
    mockClient->emitToolCalls(toolCalls);
    QCoreApplication::processEvents();
    
    EXPECT_TRUE(errorEmitted);
}