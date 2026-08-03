#include "projecthistorydialog.h"

#include <ElaPushButton.h>
#include <ElaText.h>
#include <ElaContentDialog.h>

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
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setMinimumSize(520, 380);
    resize(560, 420);

    // MODIFIED: 对话框整体暖灰白背景
    setStyleSheet("QDialog { background-color: #f5f7fa; }");

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    // 说明文字
    ElaText *header = new ElaText("选择最近打开的项目：", this);
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
            this, &ProjectHistoryDialog::onItemDoubleClicked);

    // 按钮行
    QHBoxLayout *btnLayout = new QHBoxLayout;
    btnLayout->setSpacing(8);

    deleteBtn_ = new ElaPushButton("删除", this);
    deleteBtn_->setMinimumWidth(80);
    deleteBtn_->setAutoDefault(false);
    // MODIFIED: 删除按钮改为次要样式（灰底暖字）
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
    connect(deleteBtn_, &ElaPushButton::clicked, this, &ProjectHistoryDialog::onDeleteClicked);
    btnLayout->addWidget(deleteBtn_);
    btnLayout->addStretch();

    ElaPushButton *closeBtn = new ElaPushButton("取消", this);
    closeBtn->setMinimumWidth(80);
    closeBtn->setAutoDefault(false);
    // MODIFIED: 取消按钮同样次要样式
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
    openBtn_->setAutoDefault(true);
    openBtn_->setDefault(true);
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
        // MODIFIED: 空状态文字改为暖灰
        emptyItem->setForeground(QColor("#8a8a9a"));
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

        QString displayText = name + "\n";
        displayText += QString("    %1").arg(path);
        if (!convTitle.isEmpty() && convTitle != "新对话" && convTitle != "项目对话") {
            displayText += QString("\n    %1").arg(convTitle);
        }
        if (!lastOpened.isEmpty()) {
            QDateTime dt = QDateTime::fromString(lastOpened, Qt::ISODate);
            if (dt.isValid()) {
                displayText += QString("  |  %1").arg(dt.toLocalTime().toString("yyyy-MM-dd HH:mm"));
            } else {
                displayText += QString("  |  %1").arg(lastOpened);
            }
        }

        QListWidgetItem *item = new QListWidgetItem(displayText);
        item->setData(Qt::UserRole, path);
        item->setData(Qt::UserRole + 1, entry["conversationId"].toString());
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

    QMessageBox msgBox(this);
    // 去掉标题栏，连带图标、标题、叉号一起消失
    msgBox.setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
    msgBox.setMinimumWidth(480);

    // 标题栏已隐藏，不需要 setWindowTitle
    msgBox.setText("确定要从历史记录中删除该项目吗？");
    msgBox.setInformativeText(
        QString("<b>%1</b><br><br>"
                "<span style='color:#8a8a9a; font-size:12px;'>此操作不会删除项目文件。</span>")
            .arg(path.toHtmlEscaped())
        );
    msgBox.setTextFormat(Qt::RichText);
    msgBox.setIcon(QMessageBox::NoIcon);   // 去掉左侧默认的大问号图标

    msgBox.setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
    msgBox.button(QMessageBox::Cancel)->setText("取消");
    msgBox.button(QMessageBox::Ok)->setText("删除");
    msgBox.setDefaultButton(QMessageBox::Cancel);

    msgBox.setStyleSheet(
        "QMessageBox {"
        "   background-color: #f5f7fa;"   // 加深
        "   border: 1px solid rgba(150, 170, 200, 0.35);"
        "   border-radius: 8px;"
        "}"
        "QLabel { color: #2a2a3a; font-size: 14px; }"   // 文字也略加深
        "QPushButton {"
        "   background: rgba(150, 170, 200, 0.3);"       // 按钮底色加深
        "   color: #2a2a3a;"
        "   border: none;"
        "   border-radius: 8px;"
        "   padding: 8px 24px;"
        "   font-size: 13px;"
        "   min-width: 80px;"
        "}"
        "QPushButton:hover { background: rgba(150, 170, 200, 0.5); }"
        "QPushButton:default {"
        "   background: rgba(217, 85, 85, 0.9);"       // 强调按钮也略深
        "   color: white;"
        "}"
        "QPushButton:default:hover { background: rgba(217, 85, 85, 1.0); }"
        );
    if (msgBox.exec() != QMessageBox::Ok) return;
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

    populateList();
    openBtn_->setEnabled(false);
}