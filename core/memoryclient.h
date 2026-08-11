#ifndef MEMORYCLIENT_H
#define MEMORYCLIENT_H

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QList>

class QProcess;

// LLM 事实记忆客户端：异步调用 python/azur_tools/memory_cli.py 抽取记忆，并写入
// AppDataLocation 下的 memory/facts.json。
//
// 设计要点：
// - 只依赖 QProcess，不阻塞 UI 线程（与 TtsClient 同款模式）。
// - 记忆抽取是「尽力而为」的副作用：失败不影响对话，只发 failed 信号 + 保留旧文件。
// - 同一时刻只保留一个抽取任务（pending 标志由 MainWindow 配合控制，本类内部
//   也会杀掉进行中的旧进程避免重叠）。
// - facts.json 的合并 / 去重 / 原子写入都在 C++ 侧完成（可用单元测试覆盖），
//   memory_cli.py 只负责「LLM 抽取 + 返回新事实 JSON 数组」。
//
// 注入侧（buildFactsBlock）是纯函数，读文件 → 渲染成 "[记忆] ..." 片段，
// 供 MainWindow 在拼 post-history 指令时调用，也方便单测直接验证。
class MemoryClient : public QObject
{
    Q_OBJECT
public:
    explicit MemoryClient(QObject *parent = nullptr);
    ~MemoryClient() override;

    // 异步触发一次记忆抽取。apiKey/baseUrl/model 与会话同源（设置页配置）。
    // 内部会读取已有 facts.json 作为 existingFacts 传给 memory_cli.py。
    void updateMemory(const QString &apiKey, const QString &baseUrl, const QString &model,
                      const QList<QJsonObject> &messages);

    // 供测试/部署覆盖默认路径（默认自动从 ToolExecutor 定位解释器与 memory_cli.py）
    void setInterpreterPath(const QString &path);
    void setCliPath(const QString &path);
    // 覆盖 facts.json 路径（默认 AppDataLocation/memory/facts.json），供单测注入临时目录
    void setFactsPath(const QString &path);

    // ---- 纯函数（不依赖网络/进程，供单测直接覆盖）----

    // 读取并合并：读已有 facts，合并新 facts（同 key 新值覆盖旧值），原子写回。
    // 返回写入后的总条数；失败返回 -1。与 startProcess 里私有的同名实现一致。
    static int mergeFacts(const QString &factsPath, const QJsonArray &newFacts);

    // 渲染注入片段：读 facts.json → "[记忆] 用户叫小明；喜欢龙井。"。
    // facts 为空 / 文件不存在 → 返回空串。
    static QString buildFactsBlock(const QString &factsPath);

    // 默认的 facts.json 路径
    static QString defaultFactsPath();

signals:
    void memoryUpdated(int factCount);       // 本次合并后 facts.json 里的总条数
    void failed(const QString &errorMessage);

private slots:
    void onFinished();
    void onErrorOccurred();
    void onTimeout();

private:
    // interpreter/cli 由调用方（updateMemory）解析后传入，避免直接使用未设置的成员
    // 导致 QProcess::start 拿到空程序名而同步触发 FailedToStart 后继续写空指针。
    void startProcess(const QString &interpreter, const QString &cli,
                      const QString &apiKey, const QString &baseUrl, const QString &model,
                      const QList<QJsonObject> &messages);
    void cleanupProc();

    QProcess *proc_ = nullptr;
    QString interpreter_;
    QString cli_;
    QString factsPath_;
    bool timeoutTriggered_ = false;
};

#endif // MEMORYCLIENT_H
