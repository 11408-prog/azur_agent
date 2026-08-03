#include "project/projectconvdialog.h"
#include "data/conversationmanager.h"

#include <ElaPushButton.h>
#include <ElaText.h>
#include <ElaContentDialog.h>

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

    // MODIFIED: 对话框整体暖灰白背景
    setStyleSheet("QDialog { background-color: #f5f7fa; }");

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    // 说明文字
    ElaText *header = new ElaText("选择要打开的项目对话：", this);
    header->setTextStyle(ElaTextType::Body);
    // MODIFIED: 标题颜色改为暖深棕
    header->setStyleSheet("color: #2a2a3a;");
    QFont f = header->font();
    f.setBold(true);
    header->setFont(f);
    mainLayout->addWidget(header);

    // 列表
    listWidget_ = new QListWidget(this);
    listWidget_->setAlternatingRowColors(false);  // MODIFIED: 关闭深色交替行
    // MODIFIED: 列表样式改为暖色调
    listWidget_->setStyleSheet(
        "QListWidget {"
        "   background: transparent;"
        "   border: 1px solid rgba(150, 170, 200, 0.35);"
        "   border-radius: 8px;"
        "   color: #3a3a4a;"
        "   padding: 4px;"
        "   outline: none;"
        "}"
        "QListWidget::item {"
        "   padding: 10px 12px;"
        "   border-radius: 6px;"
        "   margin: 2px 4px;"
        "}"
        "QListWidget::item:selected {"
        "   background-color: rgba(15, 95, 240, 0.18);"
        "   color: #2a2a3a;"
        "}"
        "QListWidget::item:hover {"
        "   background-color: rgba(150, 170, 200, 0.15);"
        "}"
        "QListWidget::item:alternate { background: transparent; }"
        "QScrollBar:vertical { background: transparent; width: 5px; margin: 0; }"
        "QScrollBar::handle:vertical { background: rgba(120, 120, 130, 0.3); border-radius: 3px; min-height: 24px; }"
        "QScrollBar::handle:vertical:hover { background: rgba(120, 120, 130, 0.5); }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        );
    mainLayout->addWidget(listWidget_, 1);

    connect(listWidget_, &QListWidget::itemDoubleClicked,
            this, &ProjectConvDialog::onItemDoubleClicked);

    // 按钮行
    QHBoxLayout *btnLayout = new QHBoxLayout;
    btnLayout->setSpacing(8);

    ElaPushButton *newBtn = new ElaPushButton("新建", this);
    newBtn->setMinimumWidth(80);
    // MODIFIED: 新建按钮用次要样式
    newBtn->setStyleSheet(
        "ElaPushButton {"
        "   background: rgba(150, 170, 200, 0.3);"
        "   color: #3a3a4a;"
        "   border: none;"
        "   border-radius: 8px;"
        "   padding: 8px 20px;"
        "   font-size: 13px;"
        "}"
        "ElaPushButton:hover {"
        "   background: rgba(150, 170, 200, 0.5);"
        "}"
        );
    connect(newBtn, &ElaPushButton::clicked, this, &ProjectConvDialog::onNewClicked);
    btnLayout->addWidget(newBtn);

    deleteBtn_ = new ElaPushButton("删除", this);
    deleteBtn_->setMinimumWidth(80);
    deleteBtn_->setEnabled(false);
    // MODIFIED: 删除按钮同样次要样式
    deleteBtn_->setStyleSheet(
        "ElaPushButton {"
        "   background: rgba(150, 170, 200, 0.3);"
        "   color: #3a3a4a;"
        "   border: none;"
        "   border-radius: 8px;"
        "   padding: 8px 20px;"
        "   font-size: 13px;"
        "}"
        "ElaPushButton:hover {"
        "   background: rgba(150, 170, 200, 0.5);"
        "}"
        );
    connect(deleteBtn_, &ElaPushButton::clicked, this, &ProjectConvDialog::onDeleteClicked);
    btnLayout->addWidget(deleteBtn_);
    btnLayout->addStretch();

    ElaPushButton *closeBtn = new ElaPushButton("取消", this);
    closeBtn->setMinimumWidth(80);
    // MODIFIED: 取消按钮次要样式
    closeBtn->setStyleSheet(
        "ElaPushButton {"
        "   background: rgba(150, 170, 200, 0.3);"
        "   color: #3a3a4a;"
        "   border: none;"
        "   border-radius: 8px;"
        "   padding: 8px 20px;"
        "   font-size: 13px;"
        "}"
        "ElaPushButton:hover {"
        "   background: rgba(150, 170, 200, 0.5);"
        "}"
        );
    connect(closeBtn, &ElaPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(closeBtn);

    openBtn_ = new ElaPushButton("打开", this);
    openBtn_->setMinimumWidth(80);
    openBtn_->setEnabled(false);
    // MODIFIED: 打开按钮用暖珊瑚色强调
    openBtn_->setStyleSheet(
        "ElaPushButton {"
        "   background: rgba(15, 95, 240, 0.85);"
        "   color: white;"
        "   border: none;"
        "   border-radius: 8px;"
        "   padding: 8px 20px;"
        "   font-size: 13px;"
        "}"
        "ElaPushButton:hover {"
        "   background: rgba(13, 82, 210, 0.95);"
        "}"
        "ElaPushButton:disabled {"
        "   background: rgba(150, 170, 200, 0.45);"
        "   color: #8a8a9a;"
        "}"
        );
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
        // MODIFIED: 空状态文字改为暖灰
        emptyItem->setForeground(QColor("#8a8a9a"));
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
    emit newConversationRequested();
    accept();
}

void ProjectConvDialog::onDeleteClicked()
{
    QListWidgetItem *item = listWidget_->currentItem();
    if (!item) return;

    const QString convId = item->data(Qt::UserRole).toString();
    if (convId.isEmpty()) return;

    // MODIFIED: 用 ElaContentDialog 替代 QMessageBox，统一暖色调风格
    ElaContentDialog dlg(this);
    dlg.setWindowTitle("确认删除");

    QWidget *centralWidget = new QWidget(&dlg);
    centralWidget->setStyleSheet("background: #f5f7fa;");
    QVBoxLayout *layout = new QVBoxLayout(centralWidget);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    ElaText *msgLabel = new ElaText(
        "确定要删除此对话吗？\n此操作不可撤销。",
        centralWidget);
    msgLabel->setTextStyle(ElaTextType::Body);
    msgLabel->setStyleSheet("color: #3a3a4a;");
    msgLabel->setWordWrap(true);
    layout->addWidget(msgLabel);

    dlg.setCentralWidget(centralWidget);
    dlg.setLeftButtonText("取消");
    dlg.setRightButtonText("删除");

    QObject::connect(&dlg, &ElaContentDialog::leftButtonClicked, &dlg, &QDialog::reject);
    QObject::connect(&dlg, &ElaContentDialog::rightButtonClicked, &dlg, &QDialog::accept);

    if (dlg.exec() != QDialog::Accepted) return;

    convMgr_->deleteConversation(convId);

    populateList();
    openBtn_->setEnabled(false);
    deleteBtn_->setEnabled(false);
}