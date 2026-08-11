#include "core/ttsclient.h"
#include "core/tool_executor.h"

#include <QProcess>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDateTime>
#include <QTimer>
#include <QStandardPaths>
#include <QDebug>

namespace {
constexpr int kTimeoutMs = 30000;   // 单次合成最长等待 30 秒，超时杀掉
constexpr int kMaxCacheFiles = 20;  // tts_cache 里最多保留的 mp3 数量
} // namespace

TtsClient::TtsClient(QObject *parent)
    : QObject(parent)
{
}

TtsClient::~TtsClient()
{
    if (proc_) {
        proc_->kill();
        proc_->waitForFinished(1000);
        delete proc_;
        proc_ = nullptr;
    }
}

void TtsClient::setInterpreterPath(const QString &path)
{
    interpreter_ = path;
}

void TtsClient::setCliPath(const QString &path)
{
    cli_ = path;
}

QString TtsClient::ttsCacheDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + "/tts_cache";
}

void TtsClient::cleanupOldFiles() const
{
    const QString dirPath = ttsCacheDir();
    QDir dir(dirPath);
    if (!dir.exists()) return;

    // QDir::Time 按修改时间排序，最旧的在最后，超出上限就逐个删除
    QFileInfoList infos = dir.entryInfoList({"*.mp3"}, QDir::Files, QDir::Time);
    while (infos.size() > kMaxCacheFiles) {
        QFile::remove(infos.takeLast().absoluteFilePath());
    }
}

void TtsClient::synthesize(const QString &text, const QString &voice)
{
    if (text.trimmed().isEmpty()) {
        emit failed("合成失败：文本为空");
        return;
    }

    // 同一时刻只保留一个合成任务：杀掉进行中的旧任务，避免重叠
    if (proc_) {
        proc_->kill();
        proc_->disconnect(this);
        proc_->deleteLater();
        proc_ = nullptr;
        timeoutTriggered_ = false;
    }

    QDir().mkpath(ttsCacheDir());
    cleanupOldFiles();

    const QString outputPath = ttsCacheDir() + "/reply_"
        + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz") + ".mp3";

    QString interpreter = interpreter_;
    if (interpreter.isEmpty()) {
        interpreter = ToolExecutor::pythonInterpreterPath();
    }
    QString cli = cli_;
    if (cli.isEmpty()) {
        // tts_cli.py 与 cli.py 同目录
        const QString toolCli = ToolExecutor::toolCliPath();
        cli = QFileInfo(toolCli).absolutePath() + "/tts_cli.py";
    }

    if (interpreter.isEmpty() || cli.isEmpty() || !QFile::exists(cli)) {
        emit failed("合成失败：找不到 Python 解释器或 tts_cli.py");
        return;
    }

    startProcess(interpreter, cli, text, voice, outputPath);
}

void TtsClient::startProcess(const QString &interpreter, const QString &cli,
                             const QString &text, const QString &voice, const QString &outputPath)
{
    proc_ = new QProcess(this);
    proc_->setProcessChannelMode(QProcess::SeparateChannels);

    connect(proc_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &TtsClient::onFinished);
    connect(proc_, &QProcess::errorOccurred, this, &TtsClient::onErrorOccurred);

    // 超时保护：edge-tts 需要联网合成，可能挂起，30 秒没结果就杀掉。
    // 定时器作为 proc_ 的子对象，proc_ 清理时一并销毁。
    QTimer *timer = new QTimer(proc_);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, &TtsClient::onTimeout);
    timer->start(kTimeoutMs);

    QJsonObject req;
    req["text"] = text;
    req["voice"] = voice;
    req["output"] = outputPath;

    proc_->start(interpreter, {cli});
    // start() 失败（如 FailedToStart）时，errorOccurred 会同步触发并走 onErrorOccurred
    // → cleanupProc() 把 proc_ 置空。这里必须检查，否则下面的 write 会解引用空指针崩溃。
    if (!proc_) return;
    // QProcess 会缓冲写入，进程启动后自然送过去；不用 waitForStarted，避免阻塞 UI。
    proc_->write(QJsonDocument(req).toJson(QJsonDocument::Compact));
    proc_->closeWriteChannel();
}

void TtsClient::onFinished()
{
    // 防御：若进程因启动失败已在 onErrorOccurred 里被 cleanupProc 清理（proc_ 已置空），
    // 这里直接返回，避免继续读已释放的 QProcess。正常情况下 start 成功才会走到这。
    if (!proc_) return;

    QString errorMsg;

    if (timeoutTriggered_) {
        // onTimeout 已经 kill 了进程，这里直接报超时，不再解析（stdout 可能是空的）
        errorMsg = "合成失败：超时";
    } else {
        const QByteArray out = proc_->readAllStandardOutput();
        const QByteArray err = proc_->readAllStandardError();
        if (!err.trimmed().isEmpty()) {
            qWarning() << "[TTS] python stderr:" << QString::fromUtf8(err);
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(out, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            errorMsg = "合成失败：无法解析后端响应";
        } else {
            const QJsonObject resp = doc.object();
            if (resp.value("ok").toBool()) {
                emit synthesized(resp.value("content").toString());
            } else {
                errorMsg = resp.value("content").toString();
                if (errorMsg.isEmpty()) errorMsg = "合成失败";
            }
        }
    }

    if (!errorMsg.isEmpty()) emit failed(errorMsg);
    cleanupProc();
}

void TtsClient::onErrorOccurred()
{
    // FailedToStart 这类"进程根本起不来"的错误不会触发 finished，必须在这里清理。
    // Crashed 会同时触发 finished（onFinished 里兜底解析），这里不重复处理。
    if (timeoutTriggered_) return;
    if (proc_ && proc_->error() != QProcess::Crashed) {
        emit failed("合成失败：无法启动 Python 解释器");
        cleanupProc();
    }
}

void TtsClient::onTimeout()
{
    timeoutTriggered_ = true;
    if (proc_) proc_->kill();
    // kill 后 QProcess 会 emit finished(CrashExit)，由 onFinished 统一清理并报超时
}

void TtsClient::cleanupProc()
{
    if (!proc_) return;
    proc_->disconnect(this);
    proc_->deleteLater();
    proc_ = nullptr;
    timeoutTriggered_ = false;
}
