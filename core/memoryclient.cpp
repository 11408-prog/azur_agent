#include "core/memoryclient.h"
#include "core/tool_executor.h"

#include <QProcess>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QDateTime>
#include <QTimer>
#include <QStandardPaths>
#include <QDebug>

namespace {
constexpr int kTimeoutMs = 60000;   // 单次抽取最长等待 60 秒，超时杀掉（LLM 可能慢）
constexpr int kMaxInjectedFacts = 30;   // 注入 prompt 的事实条数上限，防 prompt 膨胀
constexpr int kMaxInjectedChars = 800;  // 注入片段最长字符数
} // namespace

MemoryClient::MemoryClient(QObject *parent)
    : QObject(parent)
{
}

MemoryClient::~MemoryClient()
{
    if (proc_) {
        proc_->kill();
        proc_->waitForFinished(1000);
        delete proc_;
        proc_ = nullptr;
    }
}

void MemoryClient::setInterpreterPath(const QString &path)
{
    interpreter_ = path;
}

void MemoryClient::setCliPath(const QString &path)
{
    cli_ = path;
}

void MemoryClient::setFactsPath(const QString &path)
{
    factsPath_ = path;
}

QString MemoryClient::defaultFactsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + "/memory/facts.json";
}

// ==================== 文件读写（纯逻辑，供单测覆盖） ====================

int MemoryClient::mergeFacts(const QString &factsPath, const QJsonArray &newFacts)
{
    // 1. 读已有 facts
    QJsonArray existing;
    {
        QFile f(factsPath);
        if (f.open(QIODevice::ReadOnly)) {
            const QByteArray raw = f.readAll();
            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                const QJsonArray arr = doc.object().value("facts").toArray();
                if (!arr.isEmpty()) existing = arr;
            }
        }
    }

    // 2. 合并（同 key 新值覆盖旧值；旧 key 保持原位置，新 key 追加到末尾）
    //    用顺序遍历代替 QMap，保持已有事实的相对顺序稳定。
    QStringList seenKeys;
    for (const QJsonValue &v : existing) {
        const QJsonObject o = v.toObject();
        const QString key = o.value("key").toString().trimmed();
        if (!key.isEmpty()) seenKeys << key;
    }

    QJsonArray merged = existing;
    for (const QJsonValue &v : newFacts) {
        const QJsonObject fact = v.toObject();
        const QString key = fact.value("key").toString().trimmed();
        if (key.isEmpty()) continue;
        const QString value = fact.value("value").toString().trimmed();
        if (value.isEmpty()) continue;

        const int idx = seenKeys.indexOf(key);
        if (idx >= 0) {
            // 覆盖旧值
            QJsonObject updated = merged[idx].toObject();
            updated["value"] = value;
            if (fact.contains("confidence")) updated["confidence"] = fact.value("confidence");
            merged[idx] = updated;
        } else {
            seenKeys << key;
            merged.append(fact);
        }
    }

    // 3. 备份后原子写回（备份目录 memory/backups/，最多保留 5 份）
    const QString backupDir = QFileInfo(factsPath).absolutePath() + "/backups";
    if (existing != merged) {
        QDir().mkpath(backupDir);
        QDir().mkpath(QFileInfo(factsPath).absolutePath());
        if (QFile::exists(factsPath)) {
            const QString backupName = "facts_"
                + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz") + ".json";
            QFile::copy(factsPath, backupDir + "/" + backupName);

            QFileInfoList backups = QDir(backupDir).entryInfoList({"facts_*.json"},
                                                                  QDir::Files, QDir::Time);
            while (backups.size() > 5) {
                QFile::remove(backups.takeLast().absoluteFilePath());
            }
        }

        QJsonObject root;
        root["updated_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        root["facts"] = merged;

        // 用 QSaveFile 做原子替换：先写临时文件，commit 时才替换原文件，
        // 避免「先删旧再改名」中途失败导致 facts.json 丢失。
        QSaveFile save(factsPath);
        if (!save.open(QIODevice::WriteOnly)) {
            qWarning() << "[MEMORY] 无法打开临时文件写入:" << factsPath;
            return -1;
        }
        save.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        if (!save.commit()) {
            qWarning() << "[MEMORY] 原子替换 facts.json 失败:" << factsPath;
            return -1;
        }
    }

    return merged.size();
}

QString MemoryClient::buildFactsBlock(const QString &factsPath)
{
    QJsonArray facts;
    {
        QFile f(factsPath);
        if (!f.open(QIODevice::ReadOnly)) return QString();
        const QByteArray raw = f.readAll();
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) return QString();
        const QJsonArray arr = doc.object().value("facts").toArray();
        if (arr.isEmpty()) return QString();
        facts = arr;
    }

    if (facts.isEmpty()) return QString();

    QStringList parts;
    int totalChars = 0;
    for (const QJsonValue &v : facts) {
        if (parts.size() >= kMaxInjectedFacts) break;
        const QJsonObject o = v.toObject();
        const QString key = o.value("key").toString().trimmed();
        const QString value = o.value("value").toString().trimmed();
        if (key.isEmpty() || value.isEmpty()) continue;
        const QString part = key + "：" + value;
        totalChars += part.length() + 2;
        if (totalChars > kMaxInjectedChars) break;
        parts << part;
    }

    if (parts.isEmpty()) return QString();
    return "[记忆] " + parts.join("；") + "。";
}

// ==================== 异步调用 ====================

void MemoryClient::updateMemory(const QString &apiKey, const QString &baseUrl, const QString &model,
                                const QList<QJsonObject> &messages)
{
    if (apiKey.trimmed().isEmpty()) {
        emit failed("记忆更新失败：apiKey 为空");
        return;
    }
    if (messages.isEmpty()) {
        emit failed("记忆更新失败：没有可抽取的对话");
        return;
    }

    // 同一时刻只保留一个抽取任务：杀掉进行中的旧任务，避免重叠
    if (proc_) {
        proc_->kill();
        proc_->disconnect(this);
        proc_->deleteLater();
        proc_ = nullptr;
        timeoutTriggered_ = false;
    }

    QString interpreter = interpreter_;
    if (interpreter.isEmpty()) interpreter = ToolExecutor::pythonInterpreterPath();

    QString cli = cli_;
    if (cli.isEmpty()) {
        const QString toolCli = ToolExecutor::toolCliPath();
        cli = QFileInfo(toolCli).absolutePath() + "/memory_cli.py";
    }

    if (interpreter.isEmpty() || cli.isEmpty() || !QFile::exists(cli)) {
        emit failed("记忆更新失败：找不到 Python 解释器或 memory_cli.py");
        return;
    }

    startProcess(interpreter, cli, apiKey, baseUrl, model, messages);
}

void MemoryClient::startProcess(const QString &interpreter, const QString &cli,
                                const QString &apiKey, const QString &baseUrl, const QString &model,
                                const QList<QJsonObject> &messages)
{
    const QString factsPath = factsPath_.isEmpty() ? defaultFactsPath() : factsPath_;

    // 读取已有 facts 作为 existingFacts（用于减少重复抽取；文件不存在则为空数组）
    QJsonArray existingFacts;
    {
        QFile f(factsPath);
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
            if (doc.isObject()) existingFacts = doc.object().value("facts").toArray();
        }
    }

    QJsonArray msgs;
    for (const QJsonObject &m : messages) {
        QJsonObject clean;
        clean["role"] = m.value("role").toString();
        clean["content"] = m.value("content").toString();
        msgs.append(clean);
    }

    proc_ = new QProcess(this);
    proc_->setProcessChannelMode(QProcess::SeparateChannels);

    connect(proc_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MemoryClient::onFinished);
    connect(proc_, &QProcess::errorOccurred, this, &MemoryClient::onErrorOccurred);

    QTimer *timer = new QTimer(proc_);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, &MemoryClient::onTimeout);
    timer->start(kTimeoutMs);

    QJsonObject req;
    req["apiKey"] = apiKey;
    req["baseUrl"] = baseUrl;
    req["model"] = model;
    req["messages"] = msgs;
    req["existingFacts"] = existingFacts;

    proc_->start(interpreter, {cli});
    // start() 失败（如 FailedToStart）时，errorOccurred 会同步触发并走 onErrorOccurred
    // → cleanupProc() 把 proc_ 置空。这里必须检查，否则下面的 write 会解引用空指针崩溃。
    if (!proc_) return;
    proc_->write(QJsonDocument(req).toJson(QJsonDocument::Compact));
    proc_->closeWriteChannel();
}

void MemoryClient::onFinished()
{
    // 防御：若进程因启动失败已在 onErrorOccurred 里被 cleanupProc 清理（proc_ 已置空），
    // 这里直接返回，避免继续读已释放的 QProcess。正常情况下 start 成功才会走到这。
    if (!proc_) return;

    QString errorMsg;

    if (timeoutTriggered_) {
        errorMsg = "记忆更新失败：超时";
    } else {
        const QByteArray out = proc_->readAllStandardOutput();
        const QByteArray err = proc_->readAllStandardError();
        if (!err.trimmed().isEmpty()) {
            qWarning() << "[MEMORY] python stderr:" << QString::fromUtf8(err);
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(out, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            errorMsg = "记忆更新失败：无法解析后端响应";
        } else {
            const QJsonObject resp = doc.object();
            if (resp.value("ok").toBool()) {
                // content 是新事实的 JSON 数组字符串
                const QJsonDocument factsDoc = QJsonDocument::fromJson(
                    resp.value("content").toString().toUtf8());
                const QJsonArray newFacts = factsDoc.isArray() ? factsDoc.array() : QJsonArray();

                const QString factsPath = factsPath_.isEmpty() ? defaultFactsPath() : factsPath_;
                const int total = mergeFacts(factsPath, newFacts);
                if (total < 0) {
                    errorMsg = "记忆更新失败：写入 facts.json 出错";
                } else {
                    qDebug() << "[MEMORY] 记忆更新完成 | 合并后事实数=" << total;
                    emit memoryUpdated(total);
                }
            } else {
                errorMsg = resp.value("content").toString();
                if (errorMsg.isEmpty()) errorMsg = "记忆更新失败";
            }
        }
    }

    if (!errorMsg.isEmpty()) emit failed(errorMsg);
    cleanupProc();
}

void MemoryClient::onErrorOccurred()
{
    if (timeoutTriggered_) return;
    if (proc_ && proc_->error() != QProcess::Crashed) {
        emit failed("记忆更新失败：无法启动 Python 解释器");
        cleanupProc();
    }
}

void MemoryClient::onTimeout()
{
    timeoutTriggered_ = true;
    if (proc_) proc_->kill();
}

void MemoryClient::cleanupProc()
{
    if (!proc_) return;
    proc_->disconnect(this);
    proc_->deleteLater();
    proc_ = nullptr;
    timeoutTriggered_ = false;
}
