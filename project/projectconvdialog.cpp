#include "project/projectconvdialog.h"
#include "data/conversationmanager.h"

#include <ElaPushButton.h>
#include <ElaText.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLabel>
#include <QMessageBox>
#include <QDateTime>
#include <QDir>
#include <QFont>

ProjectConvDialog::ProjectConvDialog(ConversationManager *convMgr,
                                      const QString &projectPath,
                                      const QString &currentConvId,
                                      QWidget *parent)
    : QDialog(parent)
    , convMgr_(convMgr)
    , projectPath_(projectPath)
    , currentConvId_(currentConvId)
    , listWidget_(nullptr)
    , openBtn_(nullptr)
    , deleteBtn_(nullptr)
{
    setWindowTitle("当前项目对话列表");
    setMinimumSize(520, 380);
    resize(560, 420);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    // 说明文字
    ElaText *header = new ElaText("选择要打开的项目对话：", this);
    header->setTextStyle(ElaTextType::Body);
    QFont f = header->font();
    f.setBold(true);
    header->setFont(f);
    mainLayout->addWidget(header);

    // 列表
    listWidget_ = new QListWidget(this);
    listWidget_->setAlternatingRowColors(true);
    listWidget_->setStyleSheet(
        "QListWidget { border: 1px solid #444; border-radius: 6px; padding: 4px; }"
        "QListWidget::item { padding: 8px; border-radius: 4px; }"
        "QListWidget::item:selected { background: #264f78; }"
        "QListWidget::item:alternate { background: #1e1e1e; }"
    );
    mainLayout->addWidget(listWidget_, 1);

    connect(listWidget_, &QListWidget::itemDoubleClicked,
            this, &ProjectConvDialog::onItemDoubleClicked);

    // 按钮行
    QHBoxLayout *btnLayout = new QHBoxLayout;
    btnLayout->setSpacing(8);

    ElaPushButton *newBtn = new ElaPushButton("新建", this);
    newBtn->setMinimumWidth(80);
    connect(newBtn, &ElaPushButton::clicked, this, &ProjectConvDialog::onNewClicked);
    btnLayout->addWidget(newBtn);

    deleteBtn_ = new ElaPushButton("删除", this);
    deleteBtn_->setMinimumWidth(80);
    deleteBtn_->setEnabled(false);
    connect(deleteBtn_, &ElaPushButton::clicked, this, &ProjectConvDialog::onDeleteClicked);
    btnLayout->addWidget(deleteBtn_);
    btnLayout->addStretch();

    ElaPushButton *closeBtn = new ElaPushButton("取消", this);
    closeBtn->setMinimumWidth(80);
    connect(closeBtn, &ElaPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(closeBtn);

    openBtn_ = new ElaPushButton("打开", this);
    openBtn_->setMinimumWidth(80);
    openBtn_->setEnabled(false);
    connect(openBtn_, &ElaPushButton::clicked, this, &ProjectConvDialog::onOpenClicked);
    btnLayout->addWidget(openBtn_);

    mainLayout->addLayout(btnLayout);

    // 选择变化的响应
    connect(listWidget_, &QListWidget::currentRowChanged, this, [this](int row) {
        openBtn_->setEnabled(row >= 0);
        deleteBtn_->setEnabled(row >= 0);
    });

    // 加载数据
    populateList();
}

void ProjectConvDialog::populateList()
{
    listWidget_->clear();

    const QString cleanPath = QDir::toNativeSeparators(QDir::cleanPath(projectPath_));
    convMeta_ = cleanPath.isEmpty()
        ? convMgr_->conversationsMeta()
        : convMgr_->conversationsForProject(cleanPath);

    if (convMeta_.isEmpty()) {
        QListWidgetItem *emptyItem = new QListWidgetItem("暂无对话记录");
        emptyItem->setFlags(emptyItem->flags() & ~Qt::ItemIsSelectable);
        emptyItem->setForeground(QColor("#888"));
        emptyItem->setSizeHint(QSize(0, 40));
        listWidget_->addItem(emptyItem);
        return;
    }

    for (const QJsonValue &val : convMeta_) {
        const QJsonObject obj = val.toObject();
        const QString id = obj["id"].toString();
        const QString title = obj["title"].toString();
        const QString updated = obj["updated"].toString();
        const int messageCount = obj["messageCount"].toInt();

        // 显示: 标题  [日期]  (N 条消息)
        QString displayText = title;
        if (!updated.isEmpty()) {
            QString dateStr = updated.left(10);
            displayText += QString("  [%1]").arg(dateStr);
        }
        displayText += QString("  (%1 条消息)").arg(messageCount);

        QListWidgetItem *item = new QListWidgetItem(displayText);
        item->setData(Qt::UserRole, id);
        item->setSizeHint(QSize(0, 36));

        // 高亮当前对话
        if (id == currentConvId_) {
            QFont boldFont = item->font();
            boldFont.setBold(true);
            item->setFont(boldFont);
            listWidget_->setCurrentItem(item);
        }

        listWidget_->addItem(item);
    }
}

void ProjectConvDialog::onOpenClicked()
{
    QListWidgetItem *item = listWidget_->currentItem();
    if (!item) return;

    const QString convId = item->data(Qt::UserRole).toString();
    if (!convId.isEmpty()) {
        emit conversationSelected(convId);
        accept();
    }
}

void ProjectConvDialog::onItemDoubleClicked(QListWidgetItem *item)
{
    if (!item) return;

    const QString convId = item->data(Qt::UserRole).toString();
    if (!convId.isEmpty()) {
        emit conversationSelected(convId);
        accept();
    }
}

void ProjectConvDialog::onNewClicked()
{
    // 关闭对话框前让 MainWindow 创建新对话
    emit newConversationRequested();
    accept();
}

void ProjectConvDialog::onDeleteClicked()
{
    QListWidgetItem *item = listWidget_->currentItem();
    if (!item) return;

    const QString convId = item->data(Qt::UserRole).toString();
    if (convId.isEmpty()) return;

    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "确认删除",
        QString("确定要删除此对话吗？\n此操作不可撤销。"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (reply != QMessageBox::Yes) return;

    convMgr_->deleteConversation(convId);

    // 重新加载列表
    populateList();
    openBtn_->setEnabled(false);
    deleteBtn_->setEnabled(false);
}
