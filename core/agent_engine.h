#ifndef AGENT_ENGINE_H
#define AGENT_ENGINE_H

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

#include "core/contextpolicy.h"

class DeepSeekClient;

// Agent 引擎：封装 AI 请求 → 流式响应 → 工具调用 → 自动循环 的完整流程。
// MainWindow 只需要创建引擎、连接信号、调用 start()，无需关心内部细节。
class AgentEngine : public QObject
{
    Q_OBJECT
public:
    explicit AgentEngine(DeepSeekClient *client, QObject *parent = nullptr);
    ~AgentEngine() override;

    // 启动一轮 Agent 循环
    //   messageHistory : 已经包含当前用户消息的完整历史（不含 system prompt，引擎内部会加）
    //   postHistoryInstructions : 历史之后的语气约束指令（P1 语气一致性）。
    //     为空则行为与现在完全一致；非空时云端模型追加一条尾部 system 消息，
    //     本地模型拼到最后一条消息的 content 里（位于历史之后、生成之前）。
    void start(const QString &apiKey, const QString &baseUrl, const QString &model,
               const QList<QJsonObject> &messageHistory,
               const QString &systemPrompt,
               const QJsonArray &tools,
               const QString &workspaceRoot,
               const QString &postHistoryInstructions = QString());

    void cancel();
    bool isRunning() const { return isRunning_; }

    // 引擎当前是否"占用中"（正在请求/流式生成）。
    bool isBusy() const { return isRunning_; }

    void setAllowedPaths(const QStringList &paths);
    void setMaxToolRounds(int rounds) { maxToolRounds_ = rounds; }

    // 获取引擎内部累积的完整消息历史（含 assistant / tool 消息）
    QList<QJsonObject> messageHistory() const { return messageHistory_; }

signals:
    // 流式文本片段
    void chunkReceived(const QString &delta);

    // 步骤状态变化（用于 UI 状态栏更新）
    void stepChanged(const QString &text);

    // 引擎完全结束（正常完成或用户拒绝后结束）
    void finished(const QString &fullText);

    // 上下文裁剪报告（每次 sendRequest 时发出，供日志/UI 调试）
    void contextTrimmed(const ContextReport &report);

    // 错误
    void errorOccurred(const QString &errorMessage);

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
    void executeToolCalls(const QJsonArray &toolCalls);

    DeepSeekClient *client_;

    // 请求参数
    QString apiKey_;
    QString baseUrl_;
    QString model_;
    QString systemPrompt_;
    QString postHistoryInstructions_;
    QJsonArray tools_;
    QString workspaceRoot_;
    QStringList allowedPaths_;

    // 运行时状态
    bool isRunning_ = false;
    int toolRound_ = 0;
    int maxToolRounds_ = 200; // 足够高避免正常任务被截断，出错时仍可防无限循环

    // 本轮累积的完整消息历史
    QList<QJsonObject> messageHistory_;
    QString buffer_;
};

#endif // AGENT_ENGINE_H
