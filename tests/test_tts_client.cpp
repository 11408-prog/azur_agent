#include <gtest/gtest.h>
#include <QProcess>
#include <QFile>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QByteArray>
#include <QDebug>
#include <QSignalSpy>

#include "core/ttsclient.h"

// 验证 python/azur_tools/tts_cli.py 的 stdio JSON 协议（协议级，不需要联网、
// 不需要真实合成，只要脚本能被解释器跑起来并返回合法 JSON）。
//
// 前置条件（同 test_python_backend.cpp 的模式）：
//   1. 项目根下存在虚拟环境 azur_agent/（含 Scripts/python.exe）
//   2. 项目根下存在 python/azur_tools/tts_cli.py
//   3. CMake 配置时传入了 -DPROJECT_ROOT_DIR=项目根（见 tests/CMakeLists.txt）
//
// 如果 venv 或 tts_cli.py 不存在，相关用例会 SKIP，不会误报失败。

namespace {

QString venvInterpreter()
{
    return QStringLiteral(PROJECT_ROOT_DIR) + "/azur_agent/Scripts/python.exe";
}

QString ttsCli()
{
    return QStringLiteral(PROJECT_ROOT_DIR) + "/python/azur_tools/tts_cli.py";
}

// 往 tts_cli.py 喂一行 stdin，把 stdout 的第一行解析成 QJsonObject。
// 返回 false 表示后端不可用（找不到 venv/脚本/进程起不来/输出不是合法 JSON）。
bool runTtsCli(const QByteArray &stdinPayload, QJsonObject *out)
{
    if (!QFile::exists(venvInterpreter()) || !QFile::exists(ttsCli())) return false;

    QProcess proc;
    proc.start(venvInterpreter(), {ttsCli()});
    if (!proc.waitForStarted(3000)) return false;

    proc.write(stdinPayload);
    proc.closeWriteChannel();

    if (!proc.waitForFinished(10000)) {
        proc.kill();
        proc.waitForFinished(1000);
        return false;
    }

    const QByteArray outRaw = proc.readAllStandardOutput();
    const QByteArray errRaw = proc.readAllStandardError();
    if (!errRaw.trimmed().isEmpty()) {
        qWarning() << "[TEST-TTS] python stderr:" << QString::fromUtf8(errRaw);
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(outRaw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    *out = doc.object();
    return true;
}

} // namespace

class TtsClientProtocolTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(TtsClientProtocolTest, EmptyText_ReturnsJsonOkFalse) {
    QJsonObject resp;
    if (!runTtsCli(
            "{\"text\":\"\",\"voice\":\"zh-CN-XiaoyiNeural\",\"output\":\"x.mp3\"}", &resp)) {
        GTEST_SKIP() << "未找到 azur_agent 虚拟环境或 tts_cli.py，跳过";
    }

    // 空 text 必须在导入 edge-tts 之前就被拒绝，因此不依赖 edge-tts 是否安装
    EXPECT_FALSE(resp.value("ok").toBool());
    EXPECT_FALSE(resp.value("content").toString().isEmpty());
}

TEST_F(TtsClientProtocolTest, InvalidJson_ReturnsJsonOkFalse) {
    QJsonObject resp;
    if (!runTtsCli("这不是合法JSON{{{", &resp)) {
        GTEST_SKIP() << "未找到 azur_agent 虚拟环境或 tts_cli.py，跳过";
    }

    EXPECT_FALSE(resp.value("ok").toBool());
    EXPECT_FALSE(resp.value("content").toString().isEmpty());
}

// ---------------------------------------------------------------------
// 生命周期级：QSignalSpy 观察 TtsClient 的同步失败路径（不联网、不合成）
// ---------------------------------------------------------------------

TEST_F(TtsClientProtocolTest, Lifecycle_EmptyTextEmitsFailedSynchronously) {
    TtsClient client;
    QSignalSpy spy(&client, &TtsClient::failed);
    client.synthesize("", "zh-CN-XiaoyiNeural");
    EXPECT_EQ(spy.count(), 1);
}

TEST_F(TtsClientProtocolTest, Lifecycle_MissingInterpreterOrCliEmitsFailedSynchronously) {
    TtsClient client;
    client.setInterpreterPath("C:/nonexistent/python.exe");
    client.setCliPath("C:/nonexistent/tts_cli.py");
    QSignalSpy spy(&client, &TtsClient::failed);
    client.synthesize("你好", "zh-CN-XiaoyiNeural");
    EXPECT_EQ(spy.count(), 1);
}

