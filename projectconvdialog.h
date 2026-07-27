#ifndef PROJECTCONVDIALOG_H
#define PROJECTCONVDIALOG_H

#include <QDialog>
#include <QJsonArray>

class QListWidget;
class QListWidgetItem;
class ElaPushButton;
class ConversationManager;

// 当前项目对话列表对话框
class ProjectConvDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ProjectConvDialog(ConversationManager *convMgr,
                                const QString &projectPath,
                                const QString &currentConvId,
                                QWidget *parent = nullptr);

signals:
    void conversationSelected(const QString &convId);

private slots:
    void onOpenClicked();
    void onDeleteClicked();
    void onItemDoubleClicked(QListWidgetItem *item);

private:
    void populateList();

    ConversationManager *convMgr_;
    QString projectPath_;
    QString currentConvId_;
    QJsonArray convMeta_;

    QListWidget *listWidget_;
    ElaPushButton *openBtn_;
    ElaPushButton *deleteBtn_;
};

#endif // PROJECTCONVDIALOG_H
