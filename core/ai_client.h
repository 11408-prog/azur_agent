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

    // ============ 修改点：添加 virtual ============
    // 以便 GMock 可以 override 这些函数
    virtual void sendMessage(const QString &apiKey, const QString &baseUrl, const QString &model,
                             const QJsonArray &messages, const QJsonArray &tools = QJsonArray());
    virtual void testConnection(const QString &apiKey, const QString &baseUrl);
    virtual void cancel();
    // ============================================

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
    static QUrl chatCompletionsUrl(const QString &baseUrl);
    static QUrl modelsUrl(const QString &baseUrl);
    static QString requestErrorMessage(QNetworkReply::NetworkError error, int statusCode,
                                       const QByteArray &responseData, const QString &errorString);

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
    bool sawToolCallFinish_ = false;
};

#endif // DEEPSEEKCLIENT_H