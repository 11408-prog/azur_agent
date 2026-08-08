#include "core/ai_client.h"

#include <QNetworkRequest>
#include <QUrl>
#include <QDebug>

DeepSeekClient::DeepSeekClient(QObject *parent)
    : QObject(parent)
    , networkManager_(new QNetworkAccessManager(this))
    , currentReply_(nullptr)
    , timeoutTimer_(new QTimer(this))
    , timeoutTriggered_(false)
    , sawToolCallFinish_(false)
{
    qDebug()<<"[AI_CLIENT] 构造 DeepSeekClient";
    timeoutTimer_->setSingleShot(true);
    connect(timeoutTimer_, &QTimer::timeout, this, [this]() {
        timeoutTriggered_ = true;
        if (currentReply_) {
            // 先取局部副本、把成员指针置空，再 abort()：
            // abort() 在部分网络后端下会同步触发 finished 信号，如果这时 currentReply_
            // 还指向即将被清理的 reply，重入进来的槽函数就可能踩到一个正在销毁的对象。
            QNetworkReply *reply = currentReply_;
            currentReply_ = nullptr;
            reply->abort();
            reply->deleteLater();
        }
        emit connectionTested(false, "连接超时，请检查网络或服务状态。");
    });
}

DeepSeekClient::~DeepSeekClient()
{
    qDebug()<<"[AI_CLIENT] 析构 DeepSeekClient";
    cancel();
}

//实现get请求
void DeepSeekClient::testConnection(const QString &apiKey,const QString &baseUrl)
{
    qDebug()<<"[AI_CLIENT] testConnection | baseUrl="<<baseUrl;
    //url校验
    const QUrl url=modelsUrl(baseUrl);
    if(!url.isValid()||url.scheme().isEmpty()||url.host().isEmpty())
    {
        emit connectionTested(false,"url格式错误，请填写正确的地址");return;
    }

    // 取消上一次的测试（如果有）
    cancel();

    timeoutTriggered_ = false;

    //构建请求对象
    QNetworkRequest request(url);
    request.setRawHeader("Authorization",("Bearer "+apiKey).toUtf8());//添加AUthorization头是openai和deepseek的认证方式
    //发起get请求
    QNetworkReply *reply=networkManager_->get(request);
    currentReply_ = reply;

    // 启动10秒超时定时器
    timeoutTimer_->start(10000);

    //连接finished信号并处理结果
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        timeoutTimer_->stop();
        if (timeoutTriggered_) {
            reply->deleteLater();
            return;
        }

        const QByteArray responseData = reply->readAll();
        const QNetworkReply::NetworkError error = reply->error();
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString errorString = reply->errorString();
        reply->deleteLater();

        if (currentReply_ == reply) {
            currentReply_ = nullptr;
        }

        // 1. 网络错误
        if (error != QNetworkReply::NoError) {
            emit connectionTested(false, requestErrorMessage(error, statusCode, responseData, errorString));
            return;
        }

        // 2. HTTP 状态码非 2xx 视为失败
        if (statusCode < 200 || statusCode >= 300) {
            const QString msg = requestErrorMessage(error, statusCode, responseData, errorString);
            emit connectionTested(false, msg);
            return;
        }

        // 3. 检查响应内容是否包含 "object" 字段且值为 "list"（OpenAI /models 标准格式）
        QJsonDocument doc = QJsonDocument::fromJson(responseData);
        if (doc.isObject() && doc.object().contains("object")) {
            const QString objType = doc.object()["object"].toString();
            if (objType == "list") {
                emit connectionTested(true, "连接成功，网址可用");
                return;
            }
        }

        // 4. 对于非标准响应但状态码 200 的情况（例如某些代理），可降级为成功
        // 但至少确保不是 404
        emit connectionTested(true, "连接成功（响应格式非标准，但服务可访问）");
    });

}

//实现post
void DeepSeekClient::sendMessage(const QString &apiKey,const QString &baseUrl,const QString &model,const QJsonArray &messages,const QJsonArray &tools)
{
    qDebug()<<"[AI_CLIENT] sendMessage | model="<<model<<"|消息数="<<messages.size()<<"|有tools="<<(tools.isEmpty()?"否":"是");
    //取消正在进行的请求
    cancel();
    buffer_.clear();//清空累积的文本
    rawResponseBuffer_.clear();
    sseLineBuffer_.clear();
    pendingToolCalls_.clear();
    sawToolCallFinish_ = false;
    //构造json结构体
    QJsonObject body;
    body["model"]=model;
    body["messages"]=messages;
    body["stream"]=true;
    if (!tools.isEmpty()) {
        body["tools"] = tools;
    }
    //创建请求对象
    QNetworkRequest request(chatCompletionsUrl(baseUrl));
    request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
    request.setRawHeader("Authorization",("Bearer "+apiKey).toUtf8());
    //超时保护机制
    //timeoutTimer_->disconnect();
    //connect(timeoutTimer_,&QTimer::timeout,this,[this]()
    //{
    //    if(!currentReply_)return ;
    //    QNetworkReply *reply=currentReply_;
    //    currentReply_=nullptr;
    //    reply->abort();
    //    reply->deleteLater();
    //    emit errorOccurred("请求超时，已自动取消");
    //});
    //timeoutTimer_->start(400000000);
    //发起post请求
    currentReply_=networkManager_->post(
        request,QJsonDocument(body).toJson(QJsonDocument::Compact)
        );
    //连接信号，处理流式数据
    connect(currentReply_,&QNetworkReply::readyRead,this,&DeepSeekClient::onReadyRead);
    connect(currentReply_,&QNetworkReply::finished,this,&DeepSeekClient::onFinished);
}
void DeepSeekClient::cancel()
{
    qDebug()<<"[AI_CLIENT] cancel 被调用";
    timeoutTimer_->stop();
    if (currentReply_) {
        // 先取局部副本、把成员指针置空，再 abort()：
        // abort() 在部分网络后端下会同步触发 finished 信号，进而同步重入
        // onFinished()——那个函数会把 currentReply_ 置空。如果这里还是先 abort()
        // 再访问 currentReply_->deleteLater()，那时 currentReply_ 可能已经被
        // onFinished() 置空了，等于对空指针调用 deleteLater()，直接崩溃。
        // 这正是"生成过程中点取消会闪退"的根因。
        QNetworkReply *reply = currentReply_;
        currentReply_ = nullptr;
        reply->abort();
        reply->deleteLater();
    }
}

void DeepSeekClient::onReadyRead()
{
    if (!currentReply_) {
        qDebug()<<"[AI_CLIENT] onReadyRead 但 currentReply_ 为空";
        return;
    }

    const QByteArray data = currentReply_->readAll();
    rawResponseBuffer_ += data;
    processIncomingData(data);
}

void DeepSeekClient::processIncomingData(const QByteArray &data)
{
    // 一次 readyRead() 拿到的字节，不保证正好是完整的一行或几行——
    // 一条 "data: {...}" 完全可能被网络分包从中间切断，分两次 readyRead() 到达。
    // 所以要维护一个持久缓冲：新数据追加进去，只处理已经出现完整 '\n' 的那些行，
    // 最后不完整的残片留到下一次继续拼接，不能直接对本次读到的数据 split('\n')。
    sseLineBuffer_ += data;

    int searchFrom = 0;
    int newlineIdx;
    while ((newlineIdx = sseLineBuffer_.indexOf('\n', searchFrom)) != -1) {
        const QByteArray line = sseLineBuffer_.mid(searchFrom, newlineIdx - searchFrom);
        searchFrom = newlineIdx + 1;

        if (!line.startsWith("data: ")) continue;
        QByteArray payload = line.mid(6).trimmed();
        if (payload == "[DONE]") continue;

        QJsonDocument doc = QJsonDocument::fromJson(payload);
        if (doc.isNull() || !doc.isObject()) continue;

        QJsonObject obj = doc.object();
        QJsonArray choices = obj["choices"].toArray();
        if (choices.isEmpty()) continue;

        QJsonObject choice = choices[0].toObject();
        QJsonObject delta = choice["delta"].toObject();

        QString content = delta["content"].toString();
        if (!content.isEmpty()) {
            buffer_ += content;
            emit chunkReceived(content);
        }

        // 流式的tool_calls是分片吐出来的：id/name一般一次给全，
        // arguments是一段一段的JSON字符串片段，必须按index累积拼接，
        // 等这一轮流结束（finish_reason=="tool_calls"）才拼得完整
        if (delta.contains("tool_calls")) {
            const QJsonArray tcDeltas = delta["tool_calls"].toArray();
            for (const QJsonValue &v : tcDeltas) {
                const QJsonObject tc = v.toObject();
                const int idx = tc["index"].toInt();
                PendingToolCall &pending = pendingToolCalls_[idx];
                if (tc.contains("id") && !tc["id"].toString().isEmpty()) {
                    pending.id = tc["id"].toString();
                }
                const QJsonObject func = tc["function"].toObject();
                if (func.contains("name")) {
                    pending.name += func["name"].toString();
                }
                if (func.contains("arguments")) {
                    pending.argumentsJson += func["arguments"].toString();
                }
            }
        }

        const QString finishReason = choice["finish_reason"].toString();
        if (finishReason == "tool_calls") {
            sawToolCallFinish_ = true;
        }
    }

    // 只保留还没遇到换行符的尾部残片，已处理完的部分从缓冲区移除，避免缓冲区无限增长
    sseLineBuffer_ = sseLineBuffer_.mid(searchFrom);
}

QJsonArray DeepSeekClient::pendingToolCallsAsJson() const
{
    QJsonArray toolCalls;
    for (auto it = pendingToolCalls_.constBegin(); it != pendingToolCalls_.constEnd(); ++it) {
        QJsonObject tc;
        tc["id"] = it.value().id;
        tc["type"] = "function";
        QJsonObject func;
        func["name"] = it.value().name;
        func["arguments"] = it.value().argumentsJson;
        tc["function"] = func;
        toolCalls.append(tc);
    }
    return toolCalls;
}

void DeepSeekClient::onFinished()
{
    timeoutTimer_->stop();
    if (!currentReply_) {
        qDebug()<<"[AI_CLIENT] onFinished 但 currentReply_ 为空";
        return;
    }
    qDebug()<<"[AI_CLIENT] onFinished | sawToolCallFinish_="<<sawToolCallFinish_<<"|pendingToolCalls_大小="<<pendingToolCalls_.size()<<"|buffer_长度="<<buffer_.length();
    QNetworkReply *reply = currentReply_;
    currentReply_ = nullptr;

    // 先取出所有需要的数据
    const QByteArray responseData = rawResponseBuffer_ + reply->readAll();
    const QNetworkReply::NetworkError error = reply->error();
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString errorString = reply->errorString();

    // 此时可以安全删除 reply
    reply->deleteLater();

    if (error != QNetworkReply::NoError) {
        emit errorOccurred(requestErrorMessage(error, statusCode, responseData, errorString));
        return;
    }

    // 流式响应里模型请求了工具调用：把分片拼好的完整工具调用列表发出去，
    // 这一轮到此为止，不算最终回复（responseCompleted 不会触发）
    if (sawToolCallFinish_ && !pendingToolCalls_.isEmpty()) {
        emit toolCallsReceived(pendingToolCallsAsJson());
        return;
    }

    if (buffer_.isEmpty()) {
        // 兜底：极少数OpenAI兼容服务即使传了stream:true也可能直接返回非流式的完整JSON，
        // 这里再尝试从非流式格式里取内容（包括非流式格式下的tool_calls）
        const QJsonDocument doc = QJsonDocument::fromJson(responseData);
        const QJsonArray choices = doc.object()["choices"].toArray();
        const QJsonObject message = choices.isEmpty()
            ? QJsonObject()
            : choices.first().toObject()["message"].toObject();
        const QString content = message["content"].toString();
        const QJsonArray toolCalls = message["tool_calls"].toArray();

        if (!toolCalls.isEmpty()) {
            emit toolCallsReceived(toolCalls);
            return;
        }
        if (!content.isEmpty()) {
            buffer_ = content;
            emit chunkReceived(content);
        } else {
            emit errorOccurred("接口未返回可识别的对话内容，请确认 Base URL 支持 OpenAI Chat Completions API。");
            return;
        }
    }

    emit responseCompleted(buffer_);
}

QUrl DeepSeekClient::chatCompletionsUrl(const QString &baseUrl)
{
    QString endpoint = baseUrl.trimmed();
    while (endpoint.endsWith('/')) {
        endpoint.chop(1);
    }
    if (!endpoint.endsWith("/chat/completions")) {
        endpoint += "/chat/completions";
    }
    return QUrl(endpoint);
}

QUrl DeepSeekClient::modelsUrl(const QString &baseUrl)
{
    QString endpoint = baseUrl.trimmed();
    while (endpoint.endsWith('/')) {
        endpoint.chop(1);
    }
    if (endpoint.endsWith("/chat/completions")) {
        endpoint.chop(QString("/chat/completions").size());
    }
    return QUrl(endpoint + "/models");
}

QString DeepSeekClient::requestErrorMessage(QNetworkReply::NetworkError error, int statusCode,
                                            const QByteArray &responseData, const QString &errorString)
{
    if (statusCode == 401 || statusCode == 403) {
        return "认证失败，请检查 API Key 或服务权限。";
    }
    if (statusCode == 404) {
        return "接口地址不可用，请检查 Base URL 是否正确。";
    }

    // 尝试从响应体解析服务端错误信息
    const QJsonDocument doc = QJsonDocument::fromJson(responseData);
    const QString serviceMessage = doc.object()["error"].toObject()["message"].toString();
    if (!serviceMessage.isEmpty()) {
        return serviceMessage;
    }

    // 根据 Qt 网络错误码返回友好提示
    if (error == QNetworkReply::HostNotFoundError) {
        return "无法找到服务器，请检查 Base URL 的域名或网络。";
    }
    if (error == QNetworkReply::ConnectionRefusedError) {
        return "服务器拒绝连接，请检查 Base URL 和服务状态。";
    }
    // 其他错误
    return "请求失败：" + errorString;
}
