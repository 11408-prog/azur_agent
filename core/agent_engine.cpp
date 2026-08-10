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
                         const QString &workspaceRoot,
                         const QString &postHistoryInstructions)
{
    cancel();

    apiKey_ = apiKey;
    baseUrl_ = baseUrl;
    model_ = model;
    systemPrompt_ = systemPrompt;
    postHistoryInstructions_ = postHistoryInstructions;
    tools_ = tools;
    workspaceRoot_ = workspaceRoot;
    messageHistory_ = messageHistory;
    buffer_.clear();
    toolRound_ = 0;

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
    if (!isRunning_) return;
    disconnectClient();
    client_->cancel();
    isRunning_ = false;
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

    // 检测是否为本地模型（Ollama / vLLM / LM Studio 等）
    // 本地模型对 role=system 的遵循度通常很差，需要把 system prompt 注入到第一个 user 消息中
    const bool isLocalModel = baseUrl_.contains("localhost")
                              || baseUrl_.contains("127.0.0.1")
                              || baseUrl_.contains(":11434")   // Ollama 默认端口
                              || baseUrl_.contains(":8000")   // vLLM 常见端口
                              || baseUrl_.contains(":8080");  // 其它本地服务常见端口

    QJsonArray messages;

    // 云端模型：正常走 system role
    if (!systemPrompt_.isEmpty() && !isLocalModel) {
        QJsonObject systemMsg;
        systemMsg["role"] = "system";
        systemMsg["content"] = systemPrompt_;
        messages.append(systemMsg);
    }

    // 组装消息列表
    bool firstUserInjected = false;
    for (const QJsonObject &msg : trimmed) {
        QJsonObject m = msg;
        // 本地模型：把 system prompt 拼接到第一个 user 消息前面
        if (isLocalModel && !firstUserInjected && m["role"].toString() == "user") {
            QString userContent = m["content"].toString();
            m["content"] = systemPrompt_ + "\n\n---\n\n" + userContent;
            firstUserInjected = true;
        }
        messages.append(m);
    }

    // 注入"历史之后"的语气约束指令（P1 语气一致性）。位置在历史之后、生成之前，
    // 是离生成点最近的一段指令，能有效防止对话变长后人设漂移。
    // 为空时跳过，行为与现在完全一致。
    if (!postHistoryInstructions_.isEmpty() && !messages.isEmpty()) {
        if (isLocalModel) {
            // 本地模型对 role=system 遵循度差：拼到最后一条消息的 content 里。
            // 正常流程中这就是当前这条 user 消息，恰好位于历史之后、生成之前。
            QJsonObject last = messages.last().toObject();
            last["content"] = last["content"].toString()
                              + "\n\n---\n\n" + postHistoryInstructions_;
            messages[messages.size() - 1] = last;
        } else {
            // 云端模型：追加一条尾部 system 消息
            QJsonObject instrMsg;
            instrMsg["role"] = "system";
            instrMsg["content"] = postHistoryInstructions_;
            messages.append(instrMsg);
        }
    }

    //打印最终发给模型的消息
    qDebug() << "[ENGINE] ====== 最终请求消息 ======";
    qDebug() << "[ENGINE] 模型:" << model_;
    qDebug() << "[ENGINE] 是否本地模型:" << isLocalModel;
    for (int i = 0; i < messages.size(); ++i) {
        QJsonObject m = messages[i].toObject();
        QString role = m["role"].toString();
        QString content = m["content"].toString();
        // 只打印前300字符，避免日志爆炸
        QString preview = content.left(300).replace('\n', ' ');
        qDebug() << "[ENGINE] msg[" << i << "] role=" << role
                 << "| content前300=" << preview
                 << "| content长度=" << content.length();
    }
    qDebug() << "[ENGINE] ====== 请求消息结束 ======";

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
        aiMsg["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        messageHistory_.append(aiMsg);
    }

    doFinish(fullText);
}

void AgentEngine::onToolCallsReceived(const QJsonArray &toolCalls)
{
    emit stepChanged("✓ 生成回复完成");

    // 记录 AI 的工具调用消息到历史
    QJsonObject assistantMsg;
    assistantMsg["role"] = "assistant";
    assistantMsg["content"] = QJsonValue::Null;
    assistantMsg["tool_calls"] = toolCalls;
    messageHistory_.append(assistantMsg);

    // 当前工具集里没有写操作工具（read_file / list_directory 均为只读），
    // 不存在需要用户确认的情况，直接执行。
    qDebug()<<"[ENGINE] 收到工具调用请求 | 工具数量"<<toolCalls.size();
    executeToolCalls(toolCalls);
}

void AgentEngine::onError(const QString &errorMessage)
{
    doFail(errorMessage);
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
    emit stepChanged("✗ 任务失败");
    emit errorOccurred(errorMessage);
}

void AgentEngine::setAllowedPaths(const QStringList &paths)
{
    allowedPaths_ = paths;
    ToolExecutor::setAllowedPaths(paths);
}
