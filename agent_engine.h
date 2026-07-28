#ifndef AGENT_ENGINE_H
#define AGENT_ENGINE_H

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

class DeepSeekClient;

// Agent 引擎：封装 AI 请求 → 流式响应 → 工具调用 → 自动循环 的完整流程。
// MainWindow / ProjectPage 只需要创建引擎、连接信号、调用 start()，无需关心内部细节。
class AgentEngine : public QObject
{
    Q_OBJECT
public:
    explicit AgentEngine(DeepSeekClient *client, QObject *parent = nullptr);
    ~AgentEngine() override;

    // 启动一轮 Agent 循环
    //   messageHistory : 已经包含当前用户消息的完整历史（不含 system prompt，引擎内部会加）
    void start(const QString &apiKey, const QString &baseUrl, const QString &model,
               const QList<QJsonObject> &messageHistory,
               const QString &systemPrompt,
               const QJsonArray &tools,
               const QString &workspaceRoot);

    void cancel();
    bool isRunning() const { return isRunning_; }

    // 引擎当前是否"占用中"（正在请求/流式生成，或者正等待用户确认写操作）。
    // Chat 模式和 Project 模式共用同一个 AgentEngine 实例，任何一方在发起新请求前
    // 都应该先检查这个状态，避免在对方请求还没结束时把它悄悄 cancel 掉。
    bool isBusy() const { return isRunning_ || waitingConfirm_; }

    void setAllowedPaths(const QStringList &paths);
    void setMaxToolRounds(int rounds) { maxToolRounds_ = rounds; }

    // 对应设置页"Agent 权限"选项：true = 自动执行写操作/命令，不弹确认框；
    // false（默认）= 每次都需要用户在弹窗里确认。调用方应在 start() 之前设置好。
    void setAutoExecute(bool autoExecute) { autoExecute_ = autoExecute; }
    bool autoExecute() const { return autoExecute_; }

    // 获取引擎内部累积的完整消息历史（含 assistant / tool 消息）
    QList<QJsonObject> messageHistory() const { return messageHistory_; }

signals:
    // 流式文本片段
    void chunkReceived(const QString &delta);

    // 步骤状态变化（用于 UI 状态栏更新）
    void stepChanged(const QString &text);

    // 引擎完全结束（正常完成或用户拒绝后结束）
    void finished(const QString &fullText);

    // 错误
    void errorOccurred(const QString &errorMessage);

    // 需要用户确认写操作（参数为 diff 预览列表）
    void writeConfirmationRequired(const QStringList &diffList);

public slots:
    // 用户对写操作确认的结果
    void confirmWrite(bool accepted);

private slots:
    void onChunkReceived(const QString &delta);
    void onResponseCompleted(const QString &fullText);
    void onToolCallsReceived(const QJsonArray &toolCalls);
    void onError(const QString &errorMessage);

private:
    void sendRequest();
    void connectClient();
    void disconnectClient();
    void doFinish(const QString &fullText);
    void doFail(const QString &errorMessage);
    QStringList previewDiff(const QJsonArray &toolCalls);
    void executeToolCalls(const QJsonArray &toolCalls);

    DeepSeekClient *client_;

    // 请求参数
    QString apiKey_;
    QString baseUrl_;
    QString model_;
    QString systemPrompt_;
    QJsonArray tools_;
    QString workspaceRoot_;
    QStringList allowedPaths_;

    // 运行时状态
    bool isRunning_ = false;
    bool waitingConfirm_ = false;
    bool autoExecute_ = false;
    int toolRound_ = 0;
    int maxToolRounds_ = 200; // 足够高避免正常任务被截断，出错时仍可防无限循环

    // 本轮累积的完整消息历史
    QList<QJsonObject> messageHistory_;
    QString buffer_;

    // 等待确认的工具调用
    QJsonArray pendingToolCalls_;
    QStringList pendingDiffs_;
};

#endif // AGENT_ENGINE_H
