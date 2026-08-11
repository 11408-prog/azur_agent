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
#include <QSignalSpy>

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

// 写一个 stub python 脚本：读 stdin 请求，固定返回一条事实，模拟 memory_cli.py。
// 让 MemoryClient 的完整异步链路（updateMemory → QProcess → onFinished → mergeFacts）
// 不依赖真实 LLM / 网络也能被端到端测试。
QString writeMemoryStubScript(const QString &dirPath)
{
    const QString path = dirPath + "/stub_memory.py";
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        ADD_FAILURE() << "无法写 stub 脚本: " << qPrintable(path);
        return QString();
    }
    const char *script =
        "import sys, json\n"
        "sys.stdin.reconfigure(encoding='utf-8')\n"
        "sys.stdout.reconfigure(encoding='utf-8')\n"
        "raw = sys.stdin.read()\n"
        "req = json.loads(raw)\n"
        "facts = json.dumps([{'key': 'test_key', 'value': '测试值', 'confidence': 0.9}], ensure_ascii=False)\n"
        "print(json.dumps({'ok': True, 'content': facts}))\n";
    f.write(script);
    f.close();
    return path;
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

// ---------------------------------------------------------------------
// 生命周期级：QSignalSpy 观察 MemoryClient 的异步链路与失败路径
// （不用真实 LLM，成功路径用 stub python 脚本代替 memory_cli.py）
// ---------------------------------------------------------------------

TEST_F(MemoryClientTest, Lifecycle_EmptyApiKeyEmitsFailedSynchronously) {
    MemoryClient client;
    client.setFactsPath(factsPath);
    QSignalSpy spy(&client, &MemoryClient::failed);
    client.updateMemory("", "https://api.deepseek.com", "deepseek-v4-flash",
                        {makeFact("_msg", "你好")});
    EXPECT_EQ(spy.count(), 1);
}

TEST_F(MemoryClientTest, Lifecycle_EmptyMessagesEmitsFailedSynchronously) {
    MemoryClient client;
    client.setFactsPath(factsPath);
    QSignalSpy spy(&client, &MemoryClient::failed);
    client.updateMemory("sk-test", "https://api.deepseek.com", "deepseek-v4-flash", {});
    EXPECT_EQ(spy.count(), 1);
}

TEST_F(MemoryClientTest, Lifecycle_MissingInterpreterOrCliEmitsFailedSynchronously) {
    MemoryClient client;
    client.setFactsPath(factsPath);
    client.setInterpreterPath("C:/nonexistent/python.exe");
    client.setCliPath("C:/nonexistent/memory_cli.py");
    QSignalSpy spy(&client, &MemoryClient::failed);
    client.updateMemory("sk-test", "https://api.deepseek.com", "deepseek-v4-flash",
                        {makeFact("_msg", "你好")});
    EXPECT_EQ(spy.count(), 1);
}

TEST_F(MemoryClientTest, Lifecycle_StubScriptSuccessWritesFactsAndEmitsMemoryUpdated) {
    if (!QFile::exists(venvInterpreter())) {
        GTEST_SKIP() << "未找到 azur_agent 虚拟环境，跳过";
    }

    const QString stub = writeMemoryStubScript(tempDir->path());

    MemoryClient client;
    client.setInterpreterPath(venvInterpreter());
    client.setCliPath(stub);
    client.setFactsPath(factsPath);

    QSignalSpy updatedSpy(&client, &MemoryClient::memoryUpdated);
    QSignalSpy failedSpy(&client, &MemoryClient::failed);

    QJsonObject msg;
    msg["role"] = "user";
    msg["content"] = "我叫小明";
    client.updateMemory("sk-test", "https://api.deepseek.com", "deepseek-v4-flash", {msg});

    // 等异步 QProcess 完成（内部起事件循环，QProcess 信号能正常送达）
    ASSERT_TRUE(updatedSpy.wait(10000)) << "超时未收到 memoryUpdated 信号";
    EXPECT_EQ(failedSpy.count(), 0);
    EXPECT_EQ(updatedSpy.takeFirst().at(0).toInt(), 1);  // 合并后 1 条事实

    // facts.json 应已原子写回，且能被 buildFactsBlock 读出来
    const QString block = MemoryClient::buildFactsBlock(factsPath);
    EXPECT_TRUE(block.contains("test_key"));
    EXPECT_TRUE(block.contains("测试值"));
}
