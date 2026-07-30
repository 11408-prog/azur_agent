#include "core/agent_engine.h"
#include "core/ai_client.h"
#include "core/tool_executor.h"
#include "core/contextpolicy.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QDebug>

// ==================== 构造 / 析构 ====================
AgentEngine::AgentEngine(DeepSeekClient *client, QObject *parent)
    : QObject(parent), client_(client)
{
}

AgentEngine::~AgentEngine()
{
    cancel();
}

// ==================== 启动 / 取消 ====================
void AgentEngine::start(const QString &apiKey, const QString &baseUrl, const QString &model,
                         const QList<QJsonObject> &messageHistory,
                         const QString &systemPrompt,
                         const QJsonArray &tools,
                         const QString &workspaceRoot)
{
    cancel();

    apiKey_ = apiKey;
    baseUrl_ = baseUrl;
    model_ = model;
    systemPrompt_ = systemPrompt;
    tools_ = tools;
    workspaceRoot_ = workspaceRoot;
    messageHistory_ = messageHistory;
    buffer_.clear();
    toolRound_ = 0;
    waitingConfirm_ = false;
    pendingToolCalls_ = QJsonArray();
    pendingDiffs_.clear();

    isRunning_ = true;
    ToolExecutor::setAllowedPaths(allowedPaths_);

    emit stepChanged("正在连接...");
    qDebug()<<"[ENGINE]启动新回合|toolRound_="<<toolRound_
             <<"|历史消息数="<<messageHistory_.size()
             <<"|是否有工具="<<(tools_.isEmpty()?"否":"是");
    connectClient();
    sendRequest();
}

void AgentEngine::cancel()
{
    if (!isRunning_ && !waitingConfirm_) return;
    disconnectClient();
    client_->cancel();
    isRunning_ = false;
    waitingConfirm_ = false;
    pendingToolCalls_ = QJsonArray();
}

// ==================== Client 信号连接管理 ====================
void AgentEngine::connectClient()
{
    connect(client_, &DeepSeekClient::chunkReceived, this, &AgentEngine::onChunkReceived);
    connect(client_, &DeepSeekClient::responseCompleted, this, &AgentEngine::onResponseCompleted);
    connect(client_, &DeepSeekClient::toolCallsReceived, this, &AgentEngine::onToolCallsReceived);
    connect(client_, &DeepSeekClient::errorOccurred, this, &AgentEngine::onError);
}

void AgentEngine::disconnectClient()
{
    disconnect(client_, &DeepSeekClient::chunkReceived, this, &AgentEngine::onChunkReceived);
    disconnect(client_, &DeepSeekClient::responseCompleted, this, &AgentEngine::onResponseCompleted);
    disconnect(client_, &DeepSeekClient::toolCallsReceived, this, &AgentEngine::onToolCallsReceived);
    disconnect(client_, &DeepSeekClient::errorOccurred, this, &AgentEngine::onError);
}

// ==================== 发请求 ====================
void AgentEngine::sendRequest()
{
    // 裁剪上下文：仅影响本次 API 请求，不修改 messageHistory_
    ContextReport report;
    QList<QJsonObject> trimmed = ContextPolicy::trim(messageHistory_, 80, 20, 100000, 15, &report);
    qDebug()<<"[ENGINE]发送请求 | 原始消息数="<<messageHistory_.size()
             <<" | 裁剪后="<<trimmed.size()
             <<" | 本轮裁剪="<<(messageHistory_.size() - trimmed.size())
             <<" | 当前轮次="<<toolRound_;
    qDebug()<<"[CTX]"<<report.summary();

    emit contextTrimmed(report);

    QJsonArray messages;
    if (!systemPrompt_.isEmpty()) {
        QJsonObject systemMsg;
        systemMsg["role"] = "system";
        systemMsg["content"] = systemPrompt_;
        messages.append(systemMsg);
    }
    for (const QJsonObject &msg : trimmed) {
        messages.append(msg);
    }

    client_->sendMessage(apiKey_, baseUrl_, model_, messages, tools_);
}

// ==================== Client 回调 ====================
void AgentEngine::onChunkReceived(const QString &delta)
{
    buffer_ += delta;
    if (buffer_.length() == delta.length()) {
        emit stepChanged("正在生成回复...");
    }
    emit chunkReceived(delta);
}

void AgentEngine::onResponseCompleted(const QString &fullText)
{
    buffer_ = fullText;
    emit stepChanged("✓ 生成回复完成");

    if (!fullText.isEmpty()) {
        QJsonObject aiMsg;
        aiMsg["role"] = "assistant";
        aiMsg["content"] = fullText;
        messageHistory_.append(aiMsg);
    }

    doFinish(fullText);
}

void AgentEngine::onToolCallsReceived(const QJsonArray &toolCalls)
{
    emit stepChanged("✓ 生成回复完成");

    bool hasWrite = false;
    // 记录 AI 的工具调用消息到历史
    QJsonObject assistantMsg;
    assistantMsg["role"] = "assistant";
    assistantMsg["content"] = QJsonValue::Null;
    assistantMsg["tool_calls"] = toolCalls;
    messageHistory_.append(assistantMsg);
    // 检查是否有写操作
    for (const QJsonValue &v : toolCalls) {
        const QString name = v.toObject()["function"].toObject()["name"].toString();
        if (ToolExecutor::isWriteTool(name)) {
            hasWrite = true;
            break;
        }
    }
    qDebug()<<"[ENGINE] 收到工具调用请求 | 工具数量"<<toolCalls.size()
             <<" | 是否有写操作="<<(hasWrite ? "是" : "否")
             <<" | autoExecute_="<<(autoExecute_ ? "是" : "否");
    if (hasWrite && !autoExecute_) {
        qDebug()<<"[ENGINE] 触发写确认 | 等待用户响应";
        // 生成预览 diff，等待用户确认
        pendingToolCalls_ = toolCalls;
        pendingDiffs_ = previewDiff(toolCalls);
        waitingConfirm_ = true;
        emit stepChanged("等待用户确认修改...");
        emit writeConfirmationRequired(pendingDiffs_);
        return;
    }

    if (hasWrite) {
        // 设置页选择了"自动执行"：跳过确认弹窗，但仍然在步骤面板里如实标注，
        // 避免用户完全不知道 AI 自己执行了写操作/命令。
        emit stepChanged("⚙ Agent 权限=自动执行，跳过确认直接执行");
    }

    // 没有写操作，或已设置自动执行，直接执行
    executeToolCalls(toolCalls);
}

void AgentEngine::onError(const QString &errorMessage)
{
    doFail(errorMessage);
}

// ==================== 用户确认写操作 ====================
void AgentEngine::confirmWrite(bool accepted)
{
    qDebug()<<"[ENGINE] 用户确认结果： "<<(accepted ? "接受" : "拒绝");
    if (!waitingConfirm_) return;
    waitingConfirm_ = false;

    if (!accepted) {
        // 拒绝：把所有工具标记为"已拒绝"
        for (const QJsonValue &v : pendingToolCalls_) {
            const QJsonObject tc = v.toObject();
            const QString id = tc["id"].toString();
            const QString name = tc["function"].toObject()["name"].toString();

            QJsonObject toolMsg;
            toolMsg["role"] = "tool";
            toolMsg["tool_call_id"] = id;
            toolMsg["content"] = QStringLiteral("用户拒绝了工具调用 [%1]。请勿重试。").arg(name);
            messageHistory_.append(toolMsg);
        }
        pendingToolCalls_ = QJsonArray();

        emit stepChanged(QStringLiteral("\u2717 用户已拒绝修改"));

        // 拒绝后直接结束本轮
        disconnectClient();
        isRunning_ = false;
        emit finished("已拒绝修改操作。");
        return;
    }

    // 接受：执行所有工具
    emit stepChanged(QStringLiteral("\u2713 用户已确认修改"));
    const QJsonArray calls = pendingToolCalls_;
    pendingToolCalls_ = QJsonArray();
    executeToolCalls(calls);
}

// ==================== 执行工具调用 ====================
void AgentEngine::executeToolCalls(const QJsonArray &toolCalls)
{
    ToolExecutor::setAllowedPaths(allowedPaths_);
    qDebug()<<"[ENGINE] 开始执行工具 | 待执行数量="<<toolCalls.size();
    for (const QJsonValue &v : toolCalls) {
        const QJsonObject tc = v.toObject();
        const QString id = tc["id"].toString();
        const QJsonObject func = tc["function"].toObject();
        const QString name = func["name"].toString();
        const QString argsJson = func["arguments"].toString();

        QJsonObject args;
        QJsonParseError err;
        const QJsonDocument argsDoc = QJsonDocument::fromJson(argsJson.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError && argsDoc.isObject()) {
            args = argsDoc.object();
        }

        emit stepChanged(QStringLiteral(" 正在执行 %1...").arg(name));

        bool ok = false;
        QString displayLabel;
        const QString result = ToolExecutor::execute(workspaceRoot_, name, args, &ok, &displayLabel);

        emit stepChanged(QStringLiteral("%1 %2").arg(ok ? "✓" : "✗", displayLabel));

        QJsonObject toolMsg;
        toolMsg["role"] = "tool";
        toolMsg["tool_call_id"] = id;
        toolMsg["content"] = result;
        messageHistory_.append(toolMsg);
           }

    ++toolRound_;
    if (toolRound_ > maxToolRounds_) {
        doFail("工具调用轮次过多，已自动停止。");
        return;
    }

    emit stepChanged("正在处理工具结果...");

    qDebug()<<"[ENGINE] 本轮工具执行完毕，继续下一轮请求 （轮次="<<toolRound_<<")";

    emit stepChanged("✓ 本轮处理完成");
    sendRequest();
}

// ==================== Diff 预览 ====================
QStringList AgentEngine::previewDiff(const QJsonArray &toolCalls)
{
    QStringList diffs;
    for (const QJsonValue &v : toolCalls) {
        const QJsonObject tc = v.toObject();
        const QJsonObject func = tc["function"].toObject();
        const QString name = func["name"].toString();
        const QString argsJson = func["arguments"].toString();

        QJsonObject args;
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(argsJson.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject()) {
            args = doc.object();
        }

        if (ToolExecutor::isWriteTool(name)) {
            bool ok = false;
            QString displayLabel;
            const QString diff = ToolExecutor::previewDiff(workspaceRoot_, name, args, &ok, &displayLabel);
            diffs << diff;
        }
    }
    return diffs;
}

// ==================== 完成 / 失败 ====================
void AgentEngine::doFinish(const QString &fullText)
{
    disconnectClient();
    isRunning_ = false;
    emit stepChanged("✓ 任务完成");
    emit finished(fullText);
}

void AgentEngine::doFail(const QString &errorMessage)
{
    disconnectClient();
    isRunning_ = false;
    waitingConfirm_=false;
    emit stepChanged("✗ 任务失败");
    emit errorOccurred(errorMessage);
}

void AgentEngine::setAllowedPaths(const QStringList &paths)
{
    allowedPaths_ = paths;
    ToolExecutor::setAllowedPaths(paths);
}
