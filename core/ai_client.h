#ifndef DEEPSEEKCLIENT_H
#define DEEPSEEKCLIENT_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QUrl>
#include <QMap>
#include <QTimer>

class DeepSeekClient : public QObject
{
    Q_OBJECT

public:
    explicit DeepSeekClient(QObject *parent = nullptr);
    ~DeepSeekClient() override;

    // tools 留空数组时，行为和原来完全一样（不带function calling）
    // sendMessage() / cancel() 标成 virtual，是为了让 AgentEngine 的单元测试能用一个
    // 继承出来的假客户端（不发真实网络请求，手动 emit 信号模拟各种响应）替换掉真实实现，
    // 不影响生产环境下的正常行为。
    virtual void sendMessage(const QString &apiKey, const QString &baseUrl, const QString &model,
                     const QJsonArray &messages, const QJsonArray &tools = QJsonArray());
    void testConnection(const QString &apiKey, const QString &baseUrl);
    virtual void cancel();

    // ---- 纯函数，不依赖网络/实例状态，可以脱离真实请求单独测试 ----
    static QUrl chatCompletionsUrl(const QString &baseUrl);
    static QUrl modelsUrl(const QString &baseUrl);
    static QString requestErrorMessage(QNetworkReply::NetworkError error, int statusCode,const QByteArray &responseData, const QString &errorString);

    // 从原始 SSE 字节流解析出 chunkReceived 信号 / 累积工具调用分片状态。
    // 设计成不依赖 currentReply_（原来这部分逻辑写在 onReadyRead() 里，直接从
    // QNetworkReply 读取），单独抽出来是为了能在不发真实网络请求的情况下，
    // 拿构造好的字节数据单独测试这部分历史上最容易出 bug 的解析逻辑
    // （网络分包切断一行、tool_calls 按 index 分片累积这些场景）。
    void processIncomingData(const QByteArray &data);

    // 把当前已经累积的分片工具调用（pendingToolCalls_）组装成完整的
    // OpenAI tool_calls JSON 格式。onFinished() 和单元测试共用这一份实现，
    // 避免拼装逻辑两处各写一份、后续改了一处忘了改另一处。
    QJsonArray pendingToolCallsAsJson() const;

    // ---- 供测试观察内部状态用，不修改任何状态 ----
    QString currentBuffer() const { return buffer_; }
    bool sawToolCallFinish() const { return sawToolCallFinish_; }
    int pendingToolCallCount() const { return pendingToolCalls_.size(); }

signals:
    void chunkReceived(const QString &delta);
    void responseCompleted(const QString &fullText);
    // 模型请求执行工具调用时触发（流式分片已经在内部拼接完整）。
    // 触发这个信号意味着这一轮还没真正结束，不会同时触发 responseCompleted。
    void toolCallsReceived(const QJsonArray &toolCalls);
    void errorOccurred(const QString &errorMessage);
    void connectionTested(bool success, const QString &message);

private slots:
    void onReadyRead();
    void onFinished();

private:
    // 流式响应里，一次完整的工具调用会被拆成好几个SSE事件陆续吐出来：
    // id/name 一般一次给全，arguments 是一段一段的JSON字符串片段，必须按 index 累积拼接
    struct PendingToolCall {
        QString id;
        QString name;
        QString argumentsJson;
    };

    QNetworkAccessManager *networkManager_;
    QNetworkReply *currentReply_;
    QTimer *timeoutTimer_;
    bool timeoutTriggered_ = false;
    QString buffer_;
    QByteArray rawResponseBuffer_;
    QByteArray sseLineBuffer_; // 跨次readyRead拼接不完整的SSE行，避免一条"data: {...}"被网络分包切断导致内容丢失
    QMap<int, PendingToolCall> pendingToolCalls_; // key是流式分片里的index，QMap按key升序天然保证顺序
    bool sawToolCallFinish_;
};

#endif // DEEPSEEKCLIENT_H
