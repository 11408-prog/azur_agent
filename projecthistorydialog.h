#ifndef PROJECTHISTORYDIALOG_H
#define PROJECTHISTORYDIALOG_H

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>

class QListWidget;
class QListWidgetItem;
class ElaPushButton;

// 项目历史记录选择对话框
class ProjectHistoryDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ProjectHistoryDialog(QWidget *parent = nullptr);

    QString selectedPath() const { return selPath_; }
    QString selectedConversationId() const { return selConvId_; }

signals:
    void projectSelected(const QString &path, const QString &convId);

private slots:
    void onOpenClicked();
    void onDeleteClicked();
    void onItemDoubleClicked(QListWidgetItem *item);

private:
    void populateList();
    void storeSelection();

    QListWidget *listWidget_;
    ElaPushButton *openBtn_;
    ElaPushButton *deleteBtn_;

    QJsonArray history_;
    QString selPath_;
    QString selConvId_;
};

#endif // PROJECTHISTORYDIALOG_H
