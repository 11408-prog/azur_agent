#include "project/confirmdialogs.h"

#include <ElaContentDialog.h>
#include <ElaText.h>

#include <QVBoxLayout>
#include <QWidget>
#include <QTextBrowser>
#include <QFont>
#include <QDialog>
#include <QShortcut>
#include <QKeySequence>

namespace ConfirmDialogs {

bool confirmWriteOperations(QWidget *parent, const QStringList &diffList)
{
    ElaContentDialog dlg(parent);
    dlg.setWindowTitle("修改确认");

    QWidget *centralWidget = new QWidget(&dlg);
    // MODIFIED: centralWidget 背景改为暖灰白
    centralWidget->setStyleSheet("background: #f5f7fa;");
    QVBoxLayout *centralLayout = new QVBoxLayout(centralWidget);
    centralLayout->setContentsMargins(16, 16, 16, 16);
    centralLayout->setSpacing(10);

    ElaText *infoLabel = new ElaText(
        QString("AI 请求执行以下 %1 个操作：").arg(diffList.size()), centralWidget);
    infoLabel->setTextStyle(ElaTextType::Body);
    // MODIFIED: 标题颜色改为暖深棕
    infoLabel->setStyleSheet("color: #2a2a3a;");
    QFont infoFont = infoLabel->font();
    infoFont.setBold(true);
    infoLabel->setFont(infoFont);
    centralLayout->addWidget(infoLabel);

    QTextBrowser *diffBrowser = new QTextBrowser(centralWidget);
    diffBrowser->setReadOnly(true);
    diffBrowser->setFrameShape(QFrame::NoFrame);
    // MODIFIED: diff 预览改为暖色调暗底，与代码块风格统一
    diffBrowser->setStyleSheet(
        "QTextBrowser {"
        "   background: #232a38;"
        "   color: #e2e8f0;"
        "   font-family: 'JetBrains Mono', 'Consolas', monospace;"
        "   font-size: 13px;"
        "   padding: 12px;"
        "   border-radius: 10px;"
        "   border: 1px solid rgba(255,255,255,0.08);"
        "}"
        );
    diffBrowser->setPlainText(diffList.join("\n\n--------------------\n\n"));
    centralLayout->addWidget(diffBrowser, 1);

    dlg.setCentralWidget(centralWidget);
    dlg.setLeftButtonText("拒绝修改");
    dlg.setRightButtonText("接受修改");

    QObject::connect(&dlg, &ElaContentDialog::leftButtonClicked, &dlg, &QDialog::reject);
    QObject::connect(&dlg, &ElaContentDialog::rightButtonClicked, &dlg, &QDialog::accept);

    QShortcut *enterShortcut = new QShortcut(QKeySequence(Qt::Key_Return), &dlg);
    QShortcut *enterShortcut2 = new QShortcut(QKeySequence(Qt::Key_Enter), &dlg);
    QObject::connect(enterShortcut, &QShortcut::activated, &dlg, &QDialog::accept);
    QObject::connect(enterShortcut2, &QShortcut::activated, &dlg, &QDialog::accept);

    return dlg.exec() == QDialog::Accepted;
}

} // namespace ConfirmDialogs