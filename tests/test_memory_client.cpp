#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QByteArray>
#include <QDebug>

#include "core/memoryclient.h"

// ---------------------------------------------------------------------
// MemoryClient 的纯函数测试（不需要网络 / 不需要真实 LLM 调用）
//
// mergeFacts / buildFactsBlock 都不依赖进程，用临时目录写真实的 facts.json
// 验证合并去重、原子写回、注入渲染这三块最容易出错的核心逻辑。
//
// 另外附带一个协议级测试（可选）：真实跑 python/azur_tools/memory_cli.py
// 喂空/坏请求，验证它返回合法 JSON 且 ok=false。与 test_python_backend /
// test_tts_client 的模式一致——venv 或脚本不存在就 GTEST_SKIP，不误报失败。
// ---------------------------------------------------------------------

namespace {

QString venvInterpreter()
{
    return QStringLiteral(PROJECT_ROOT_DIR) + "/azur_agent/Scripts/python.exe";
}

QString memoryCli()
{
    return QStringLiteral(PROJECT_ROOT_DIR) + "/python/azur_tools/memory_cli.py";
}

// 写一个 facts.json（结构同 MemoryClient 落地格式）
void writeFacts(const QString &path, const QJsonArray &facts)
{
    QJsonObject root;
    root["facts"] = facts;
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
}

QJsonObject makeFact(const QString &key, const QString &value)
{
    QJsonObject o;
    o["key"] = key;
    o["value"] = value;
    return o;
}

} // namespace

class MemoryClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        tempDir = new QTemporaryDir();
        ASSERT_TRUE(tempDir->isValid()) << "无法创建临时目录";
        factsPath = tempDir->path() + "/facts.json";
    }

    void TearDown() override {
        delete tempDir;
    }

    QTemporaryDir *tempDir;
    QString factsPath;
};

// ---------------------------------------------------------------------
// mergeFacts：同 key 新值覆盖旧值 + 原子写回
// ---------------------------------------------------------------------

TEST_F(MemoryClientTest, MergeFacts_NewKeyAppendsExistingPreserved) {
    writeFacts(factsPath, {makeFact("user_name", "小明")});

    int total = MemoryClient::mergeFacts(factsPath, {makeFact("preference_tea", "龙井")});
    EXPECT_EQ(total, 2);

    QString block = MemoryClient::buildFactsBlock(factsPath);
    EXPECT_TRUE(block.contains("user_name"));
    EXPECT_TRUE(block.contains("小明"));
    EXPECT_TRUE(block.contains("preference_tea"));
    EXPECT_TRUE(block.contains("龙井"));
}

TEST_F(MemoryClientTest, MergeFacts_SameKeyOverwritesValue) {
    writeFacts(factsPath, {makeFact("user_name", "小明")});

    int total = MemoryClient::mergeFacts(factsPath, {makeFact("user_name", "王五")});
    EXPECT_EQ(total, 1);  // 去重：还是 1 条

    QString block = MemoryClient::buildFactsBlock(factsPath);
    EXPECT_TRUE(block.contains("王五"));
    EXPECT_FALSE(block.contains("小明"));
}

TEST_F(MemoryClientTest, MergeFacts_EmptyOrInvalidNewFactsKeepsExisting) {
    writeFacts(factsPath, {makeFact("user_name", "小明")});

    int total = MemoryClient::mergeFacts(factsPath, QJsonArray());
    EXPECT_EQ(total, 1);

    QString block = MemoryClient::buildFactsBlock(factsPath);
    EXPECT_TRUE(block.contains("小明"));
}

TEST_F(MemoryClientTest, MergeFacts_MissingValueSkipped) {
    writeFacts(factsPath, {makeFact("user_name", "小明")});

    // value 为空的事实不该被写入（防止 LLM 输出占位符污染记忆）
    int total = MemoryClient::mergeFacts(factsPath, {makeFact("empty_key", "")});
    EXPECT_EQ(total, 1);
}

// ---------------------------------------------------------------------
// buildFactsBlock：渲染注入片段
// ---------------------------------------------------------------------

TEST_F(MemoryClientTest, BuildFactsBlock_NoFileReturnsEmpty) {
    EXPECT_TRUE(MemoryClient::buildFactsBlock(factsPath).isEmpty());
}

TEST_F(MemoryClientTest, BuildFactsBlock_EmptyFactsReturnsEmpty) {
    writeFacts(factsPath, QJsonArray());
    EXPECT_TRUE(MemoryClient::buildFactsBlock(factsPath).isEmpty());
}

TEST_F(MemoryClientTest, BuildFactsBlock_PrefixMemoryAndContainsKeyValue) {
    writeFacts(factsPath, {makeFact("user_name", "小明")});
    QString block = MemoryClient::buildFactsBlock(factsPath);
    EXPECT_TRUE(block.startsWith("[记忆]"));
    EXPECT_TRUE(block.contains("user_name"));
    EXPECT_TRUE(block.contains("小明"));
}

TEST_F(MemoryClientTest, BuildFactsBlock_LimitsCount) {
    // 注入有上限（kMaxInjectedFacts），超过只渲染前若干条，防止 prompt 膨胀
    QJsonArray many;
    for (int i = 0; i < 100; ++i) {
        many.append(makeFact(QString("key_%1").arg(i), QString("值%1").arg(i)));
    }
    writeFacts(factsPath, many);

    QString block = MemoryClient::buildFactsBlock(factsPath);
    // 不应包含最后一条（说明被截断了）
    EXPECT_FALSE(block.contains("值99"));
    // 前面若干条应该有（说明没有整块丢弃）
    EXPECT_TRUE(block.contains("值0"));
}

// ---------------------------------------------------------------------
// 协议级：memory_cli.py 空/坏请求返回合法 JSON 且 ok=false
// ---------------------------------------------------------------------

TEST_F(MemoryClientTest, Protocol_EmptyApiKeyReturnsJsonOkFalse) {
    if (!QFile::exists(venvInterpreter()) || !QFile::exists(memoryCli())) {
        GTEST_SKIP() << "未找到 azur_agent 虚拟环境或 memory_cli.py，跳过";
    }

    QProcess proc;
    proc.start(venvInterpreter(), {memoryCli()});
    ASSERT_TRUE(proc.waitForStarted(3000));

    QJsonObject req;
    req["apiKey"] = "";
    req["baseUrl"] = "https://api.deepseek.com";
    req["model"] = "deepseek-v4-flash";
    req["messages"] = QJsonArray();
    proc.write(QJsonDocument(req).toJson(QJsonDocument::Compact));
    proc.closeWriteChannel();

    ASSERT_TRUE(proc.waitForFinished(10000));

    const QByteArray out = proc.readAllStandardOutput();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(out, &parseError);
    ASSERT_EQ(parseError.error, QJsonParseError::NoError);
    ASSERT_TRUE(doc.isObject());

    const QJsonObject resp = doc.object();
    EXPECT_FALSE(resp.value("ok").toBool());
    EXPECT_FALSE(resp.value("content").toString().isEmpty());
}

TEST_F(MemoryClientTest, Protocol_InvalidJsonReturnsJsonOkFalse) {
    if (!QFile::exists(venvInterpreter()) || !QFile::exists(memoryCli())) {
        GTEST_SKIP() << "未找到 azur_agent 虚拟环境或 memory_cli.py，跳过";
    }

    QProcess proc;
    proc.start(venvInterpreter(), {memoryCli()});
    ASSERT_TRUE(proc.waitForStarted(3000));

    proc.write("这不是合法JSON{{{");
    proc.closeWriteChannel();

    ASSERT_TRUE(proc.waitForFinished(10000));

    const QByteArray out = proc.readAllStandardOutput();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(out, &parseError);
    ASSERT_EQ(parseError.error, QJsonParseError::NoError);
    ASSERT_TRUE(doc.isObject());

    const QJsonObject resp = doc.object();
    EXPECT_FALSE(resp.value("ok").toBool());
    EXPECT_FALSE(resp.value("content").toString().isEmpty());
}
