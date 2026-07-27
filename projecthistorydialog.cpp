#include "projecthistorydialog.h"

#include <ElaPushButton.h>
#include <ElaText.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLabel>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDir>
#include <QMessageBox>
#include <QDateTime>

ProjectHistoryDialog::ProjectHistoryDialog(QWidget *parent)
    : QDialog(parent)
    , listWidget_(nullptr)
    , openBtn_(nullptr)
    , deleteBtn_(nullptr)
{
    setWindowTitle("项目历史记录");
    setMinimumSize(520, 380);
    resize(560, 420);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    // 说明文字
    ElaText *header = new ElaText("选择最近打开的项目：", this);
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
            this, &ProjectHistoryDialog::onItemDoubleClicked);

    // 按钮行
    QHBoxLayout *btnLayout = new QHBoxLayout;
    btnLayout->setSpacing(8);

    deleteBtn_ = new ElaPushButton("删除", this);
    deleteBtn_->setMinimumWidth(80);
    connect(deleteBtn_, &ElaPushButton::clicked, this, &ProjectHistoryDialog::onDeleteClicked);
    btnLayout->addWidget(deleteBtn_);
    btnLayout->addStretch();

    ElaPushButton *closeBtn = new ElaPushButton("取消", this);
    closeBtn->setMinimumWidth(80);
    connect(closeBtn, &ElaPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(closeBtn);

    openBtn_ = new ElaPushButton("打开", this);
    openBtn_->setMinimumWidth(80);
    openBtn_->setEnabled(false);
    connect(openBtn_, &ElaPushButton::clicked, this, &ProjectHistoryDialog::onOpenClicked);
    btnLayout->addWidget(openBtn_);

    mainLayout->addLayout(btnLayout);

    // 选择变化的响应
    connect(listWidget_, &QListWidget::currentRowChanged, this, [this](int row) {
        openBtn_->setEnabled(row >= 0);
    });

    // 加载数据
    populateList();
}

void ProjectHistoryDialog::populateList()
{
    listWidget_->clear();

    QSettings s("AzurStudio", "AzurAgent");
    history_ = s.value("projectHistory").toJsonArray();

    if (history_.isEmpty()) {
        QListWidgetItem *emptyItem = new QListWidgetItem("暂无历史记录");
        emptyItem->setFlags(emptyItem->flags() & ~Qt::ItemIsSelectable);
        emptyItem->setForeground(QColor("#888"));
        emptyItem->setSizeHint(QSize(0, 40));
        listWidget_->addItem(emptyItem);
        return;
    }

    for (int i = 0; i < history_.size(); ++i) {
        const QJsonObject entry = history_[i].toObject();
        const QString path = entry["path"].toString();
        const QString name = entry["name"].toString();
        const QString convTitle = entry["conversationTitle"].toString();
        const QString lastOpened = entry["lastOpened"].toString();

        // 格式化的显示文本
        QString displayText = name + "\n";
        displayText += QString("    %1").arg(path);
        if (!convTitle.isEmpty() && convTitle != "新对话" && convTitle != "项目对话") {
            displayText += QString("\n    %1").arg(convTitle);
        }
        if (!lastOpened.isEmpty()) {
            // 尝试格式化时间
            QDateTime dt = QDateTime::fromString(lastOpened, Qt::ISODate);
            if (dt.isValid()) {
                displayText += QString("  |  %1").arg(dt.toLocalTime().toString("yyyy-MM-dd HH:mm"));
            } else {
                displayText += QString("  |  %1").arg(lastOpened);
            }
        }

        QListWidgetItem *item = new QListWidgetItem(displayText);
        item->setData(Qt::UserRole, path);           // 存路径
        item->setData(Qt::UserRole + 1, entry["conversationId"].toString()); // 存 convId
        item->setSizeHint(QSize(0, 60));
        listWidget_->addItem(item);
    }
}

void ProjectHistoryDialog::storeSelection()
{
    QListWidgetItem *item = listWidget_->currentItem();
    if (item) {
        selPath_ = item->data(Qt::UserRole).toString();
        selConvId_ = item->data(Qt::UserRole + 1).toString();
    } else {
        selPath_.clear();
        selConvId_.clear();
    }
}

void ProjectHistoryDialog::onOpenClicked()
{
    storeSelection();
    if (!selPath_.isEmpty()) {
        emit projectSelected(selPath_, selConvId_);
        accept();
    }
}

void ProjectHistoryDialog::onItemDoubleClicked(QListWidgetItem *item)
{
    if (!item) return;
    selPath_ = item->data(Qt::UserRole).toString();
    selConvId_ = item->data(Qt::UserRole + 1).toString();
    if (!selPath_.isEmpty()) {
        emit projectSelected(selPath_, selConvId_);
        accept();
    }
}

void ProjectHistoryDialog::onDeleteClicked()
{
    QListWidgetItem *item = listWidget_->currentItem();
    if (!item) return;

    const QString path = item->data(Qt::UserRole).toString();
    if (path.isEmpty()) return;

    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "确认删除",
        QString("确定要从历史记录中删除项目 \"%1\" 吗？\n此操作不会删除项目文件。").arg(path),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (reply != QMessageBox::Yes) return;

    // 从 QSettings 中移除
    QSettings s("AzurStudio", "AzurAgent");
    QJsonArray history = s.value("projectHistory").toJsonArray();
    const QString cleanPath = QDir::toNativeSeparators(QDir::cleanPath(path));
    for (int i = 0; i < history.size(); ++i) {
        if (history[i].toObject()["path"].toString() == cleanPath) {
            history.removeAt(i);
            break;
        }
    }
    s.setValue("projectHistory", history);

    // 重新加载列表
    populateList();
    openBtn_->setEnabled(false);
}
