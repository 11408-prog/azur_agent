#ifndef ACTIVITYPANEL_H
#define ACTIVITYPANEL_H

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QTimer>
#include <QList>
#include <QString>
#include <QScrollArea>

struct ActivityEntry {
    enum Status { Pending, Completed, Failed };
    Status status = Pending;
    QString text;
};

// 实时 Agent 活动步骤展示面板。
// 接收 AgentEngine::stepChanged 信号文本，自动解析并呈现为带状态图标的条目列表。
class ActivityPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ActivityPanel(QWidget *parent = nullptr);

    // 主入口：接收 AgentEngine::stepChanged 文本，自动解析执行
    void onStepChanged(const QString &text);

    // 手动控制
    void clear();

private:
    // 解析 stepChanged 文本：
    //   "✓ xxx"  →  completeLastPending("xxx")
    //   "✗ xxx"  →  failLastPending("xxx")
    //   其他     →  addPendingActivity(text)
    void addPendingActivity(const QString &text);
    void completeLastPending(const QString &finalText);
    void failLastPending(const QString &finalText);
    void scrollToBottom();

    QVBoxLayout *layout_;
    QWidget *contentWidget_;
    QWidget *spacer_;  // 底部弹簧
    QScrollArea *scrollArea_;

    QList<ActivityEntry> entries_;

    // 每行: [icon QLabel] + [text QLabel]
    struct ActivityRow { QLabel *icon; QLabel *text; };
    QList<ActivityRow> rows_;

    QTimer *spinnerTimer_;
    int spinnerFrame_ = 0;
    static const QStringList kSpinnerFrames;
};

#endif // ACTIVITYPANEL_H
