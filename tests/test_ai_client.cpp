#include <gtest/gtest.h>
#include <QSignalSpy>
#include "core/ai_client.h"

// ---------------------------------------------------------------------
// chatCompletionsUrl() / modelsUrl()
//
// 纯字符串拼接逻辑，但历史上这类"用户填的 Base URL 到底带不带尾部斜杠、
// 带不带 /chat/completions 后缀"的归一化代码很容易在边界情况写错，
// 值得覆盖几种常见的用户输入形态。
// ---------------------------------------------------------------------

TEST(AiClientUrlTest, ChatCompletionsUrl_PlainBaseUrl_AppendsPath) {
    QUrl url = DeepSeekClient::chatCompletionsUrl("https://api.deepseek.com");
    EXPECT_EQ(url.toString(), "https://api.deepseek.com/chat/completions");
}

TEST(AiClientUrlTest, ChatCompletionsUrl_TrailingSlash_Normalized) {
    QUrl url = DeepSeekClient::chatCompletionsUrl("https://api.deepseek.com/");
    EXPECT_EQ(url.toString(), "https://api.deepseek.com/chat/completions");
}

TEST(AiClientUrlTest, ChatCompletionsUrl_MultipleTrailingSlashes_Normalized) {
    QUrl url = DeepSeekClient::chatCompletionsUrl("https://api.deepseek.com///");
    EXPECT_EQ(url.toString(), "https://api.deepseek.com/chat/completions");
}

TEST(AiClientUrlTest, ChatCompletionsUrl_AlreadyHasPath_NotDuplicated) {
    // 用户已经把完整路径填进 Base URL 的情况，不能变成 /chat/completions/chat/completions
    QUrl url = DeepSeekClient::chatCompletionsUrl("https://api.deepseek.com/chat/completions");
    EXPECT_EQ(url.toString(), "https://api.deepseek.com/chat/completions");
}

TEST(AiClientUrlTest, ChatCompletionsUrl_WhitespaceTrimmed) {
    QUrl url = DeepSeekClient::chatCompletionsUrl("  https://api.deepseek.com  ");
    EXPECT_EQ(url.toString(), "https://api.deepseek.com/chat/completions");
}

TEST(AiClientUrlTest, ModelsUrl_PlainBaseUrl_AppendsPath) {
    QUrl url = DeepSeekClient::modelsUrl("https://api.deepseek.com");
    EXPECT_EQ(url.toString(), "https://api.deepseek.com/models");
}

TEST(AiClientUrlTest, ModelsUrl_BaseUrlHasChatCompletionsSuffix_StripsIt) {
    // 用户如果把聊天配置的 Base URL 直接抄过来（带 /chat/completions），
    // modelsUrl() 得先把这段去掉，不能拼成 /chat/completions/models
    QUrl url = DeepSeekClient::modelsUrl("https://api.deepseek.com/chat/completions");
    EXPECT_EQ(url.toString(), "https://api.deepseek.com/models");
}

TEST(AiClientUrlTest, ModelsUrl_TrailingSlash_Normalized) {
    QUrl url = DeepSeekClient::modelsUrl("https://api.deepseek.com/");
    EXPECT_EQ(url.toString(), "https://api.deepseek.com/models");
}

// ---------------------------------------------------------------------
// requestErrorMessage()
// ---------------------------------------------------------------------

TEST(AiClientErrorMessageTest, Status401_ReturnsAuthError) {
    QString msg = DeepSeekClient::requestErrorMessage(
        QNetworkReply::NoError, 401, QByteArray(), "");
    EXPECT_TRUE(msg.contains("认证失败"));
}

TEST(AiClientErrorMessageTest, Status403_ReturnsAuthError) {
    QString msg = DeepSeekClient::requestErrorMessage(
        QNetworkReply::NoError, 403, QByteArray(), "");
    EXPECT_TRUE(msg.contains("认证失败"));
}

TEST(AiClientErrorMessageTest, Status404_ReturnsUrlError) {
    QString msg = DeepSeekClient::requestErrorMessage(
        QNetworkReply::NoError, 404, QByteArray(), "");
    EXPECT_TRUE(msg.contains("接口地址不可用"));
}

TEST(AiClientErrorMessageTest, ResponseBodyHasErrorMessage_UsesServiceMessage) {
    QByteArray body = R"({"error": {"message": "insufficient quota"}})";
    QString msg = DeepSeekClient::requestErrorMessage(
        QNetworkReply::UnknownNetworkError, 500, body, "some generic error");
    EXPECT_EQ(msg, "insufficient quota");
}

TEST(AiClientErrorMessageTest, HostNotFound_ReturnsFriendlyMessage) {
    QString msg = DeepSeekClient::requestErrorMessage(
        QNetworkReply::HostNotFoundError, 0, QByteArray(), "");
    EXPECT_TRUE(msg.contains("无法找到服务器"));
}

TEST(AiClientErrorMessageTest, ConnectionRefused_ReturnsFriendlyMessage) {
    QString msg = DeepSeekClient::requestErrorMessage(
        QNetworkReply::ConnectionRefusedError, 0, QByteArray(), "");
    EXPECT_TRUE(msg.contains("拒绝连接"));
}

TEST(AiClientErrorMessageTest, UnknownError_FallsBackToErrorString) {
    QString msg = DeepSeekClient::requestErrorMessage(
        QNetworkReply::UnknownNetworkError, 0, QByteArray(), "something broke");
    EXPECT_TRUE(msg.contains("something broke"));
}

// ---------------------------------------------------------------------
// processIncomingData() —— SSE 流式解析
//
// 这是历史上最容易出问题的一块：网络分包切断一行、tool_calls 按 index
// 分片累积。用 processIncomingData() 直接喂构造好的字节数据，不需要
// 真实网络请求，通过 QSignalSpy 观察 chunkReceived 信号验证解析结果。
// ---------------------------------------------------------------------

class AiClientSseTest : public ::testing::Test {
protected:
    void SetUp() override {
        client = new DeepSeekClient();
    }
    void TearDown() override {
        delete client;
    }
    DeepSeekClient *client;
};

TEST_F(AiClientSseTest, SingleCompleteLine_EmitsChunk) {
    QSignalSpy spy(client, &DeepSeekClient::chunkReceived);
    QByteArray data = "data: {\"choices\":[{\"delta\":{\"content\":\"你好\"}}]}\n";
    client->processIncomingData(data);

    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toString(), "你好");
    EXPECT_EQ(client->currentBuffer(), "你好");
}

TEST_F(AiClientSseTest, MultipleCompleteLines_EmitsMultipleChunks) {
    QSignalSpy spy(client, &DeepSeekClient::chunkReceived);
    QByteArray data =
        "data: {\"choices\":[{\"delta\":{\"content\":\"第一段\"}}]}\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"第二段\"}}]}\n";
    client->processIncomingData(data);

    ASSERT_EQ(spy.count(), 2);
    EXPECT_EQ(spy.at(0).at(0).toString(), "第一段");
    EXPECT_EQ(spy.at(1).at(0).toString(), "第二段");
    EXPECT_EQ(client->currentBuffer(), "第一段第二段");
}

TEST_F(AiClientSseTest, LineSplitAcrossTwoChunks_StillParsedCorrectly) {
    // 核心回归测试：一行 "data: {...}" 被网络分包从中间切断，
    // 分两次 processIncomingData() 到达，不应该丢内容也不应该崩溃。
    QSignalSpy spy(client, &DeepSeekClient::chunkReceived);
    QByteArray full = "data: {\"choices\":[{\"delta\":{\"content\":\"完整内容\"}}]}\n";
    int splitPoint = full.size() / 2;

    client->processIncomingData(full.left(splitPoint));
    // 还没遇到换行符，这一半不应该触发任何信号
    EXPECT_EQ(spy.count(), 0);

    client->processIncomingData(full.mid(splitPoint));
    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toString(), "完整内容");
}

TEST_F(AiClientSseTest, LineSplitByteByByte_StillParsedCorrectly) {
    // 更极端的分包场景：一次只喂一个字节，模拟最坏情况下的网络分片
    QSignalSpy spy(client, &DeepSeekClient::chunkReceived);
    QByteArray full = "data: {\"choices\":[{\"delta\":{\"content\":\"逐字节\"}}]}\n";
    for (int i = 0; i < full.size(); ++i) {
        client->processIncomingData(full.mid(i, 1));
    }
    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toString(), "逐字节");
}

TEST_F(AiClientSseTest, DoneMarker_Ignored) {
    QSignalSpy spy(client, &DeepSeekClient::chunkReceived);
    client->processIncomingData("data: [DONE]\n");
    EXPECT_EQ(spy.count(), 0);
}

TEST_F(AiClientSseTest, NonDataLine_Ignored) {
    QSignalSpy spy(client, &DeepSeekClient::chunkReceived);
    client->processIncomingData(": this is a comment line\n");
    client->processIncomingData("event: ping\n");
    EXPECT_EQ(spy.count(), 0);
}

TEST_F(AiClientSseTest, MalformedJson_SkippedWithoutCrash) {
    QSignalSpy spy(client, &DeepSeekClient::chunkReceived);
    client->processIncomingData("data: {not valid json\n");
    client->processIncomingData("data: {\"choices\":[{\"delta\":{\"content\":\"之后正常\"}}]}\n");

    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toString(), "之后正常");
}

TEST_F(AiClientSseTest, EmptyChoicesArray_SkippedWithoutCrash) {
    QSignalSpy spy(client, &DeepSeekClient::chunkReceived);
    client->processIncomingData("data: {\"choices\":[]}\n");
    EXPECT_EQ(spy.count(), 0);
}

TEST_F(AiClientSseTest, ToolCallSingleChunk_AccumulatedCorrectly) {
    QByteArray data =
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
        "\"id\":\"call_1\",\"function\":{\"name\":\"read_file\",\"arguments\":\"{\\\"path\\\":\\\"a.txt\\\"}\"}}]},"
        "\"finish_reason\":null}]}\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n";
    client->processIncomingData(data);

    EXPECT_TRUE(client->sawToolCallFinish());
    EXPECT_EQ(client->pendingToolCallCount(), 1);

    QJsonArray result = client->pendingToolCallsAsJson();
    ASSERT_EQ(result.size(), 1);
    QJsonObject tc = result.at(0).toObject();
    EXPECT_EQ(tc["id"].toString(), "call_1");
    EXPECT_EQ(tc["function"].toObject()["name"].toString(), "read_file");
    EXPECT_EQ(tc["function"].toObject()["arguments"].toString(), "{\"path\":\"a.txt\"}");
}

TEST_F(AiClientSseTest, ToolCallArgumentsSplitAcrossMultipleChunks_ConcatenatedInOrder) {
    // arguments 是最容易出 bug 的地方：真实流式响应里这个字段几乎总是被拆成
    // 好几个 SSE 事件陆续吐出来的一段段 JSON 字符串片段，必须按到达顺序原样拼接，
    // 拼错顺序或漏拼一段，生成的 JSON 参数就解析不出来。
    client->processIncomingData(
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call_1\","
        "\"function\":{\"name\":\"read_file\"}}]},\"finish_reason\":null}]}\n");
    client->processIncomingData(
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
        "\"function\":{\"arguments\":\"{\\\"path\\\":\"}}]},\"finish_reason\":null}]}\n");
    client->processIncomingData(
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
        "\"function\":{\"arguments\":\"\\\"src/main.cpp\\\"}\"}}]},\"finish_reason\":null}]}\n");
    client->processIncomingData(
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n");

    QJsonArray result = client->pendingToolCallsAsJson();
    ASSERT_EQ(result.size(), 1);
    QJsonObject tc = result.at(0).toObject();
    EXPECT_EQ(tc["function"].toObject()["arguments"].toString(), "{\"path\":\"src/main.cpp\"}");
}

TEST_F(AiClientSseTest, MultipleToolCallsByIndex_KeptSeparateAndOrdered) {
    // 一次回复里模型同时请求多个工具调用，按 index 分开累积，
    // 组装出来的顺序应该跟 index 升序一致（QMap 天然按 key 排序）
    client->processIncomingData(
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":1,\"id\":\"call_b\","
        "\"function\":{\"name\":\"list_directory\",\"arguments\":\"{}\"}}]},\"finish_reason\":null}]}\n");
    client->processIncomingData(
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call_a\","
        "\"function\":{\"name\":\"read_file\",\"arguments\":\"{}\"}}]},\"finish_reason\":null}]}\n");
    client->processIncomingData(
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n");

    QJsonArray result = client->pendingToolCallsAsJson();
    ASSERT_EQ(result.size(), 2);
    // index 0 排在前面，即便它是第二个到达的分片
    EXPECT_EQ(result.at(0).toObject()["id"].toString(), "call_a");
    EXPECT_EQ(result.at(1).toObject()["id"].toString(), "call_b");
}

TEST_F(AiClientSseTest, NoToolCallFinishReason_SawToolCallFinishStaysFalse) {
    client->processIncomingData(
        "data: {\"choices\":[{\"delta\":{\"content\":\"普通回复\"}}]}\n");
    EXPECT_FALSE(client->sawToolCallFinish());
    EXPECT_EQ(client->pendingToolCallCount(), 0);
}
