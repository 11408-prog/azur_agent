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
    QVBoxLayout *centralLayout = new QVBoxLayout(centralWidget);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(10);

    ElaText *infoLabel = new ElaText(
        QString("AI 请求执行以下 %1 个操作：").arg(diffList.size()), centralWidget);
    infoLabel->setTextStyle(ElaTextType::Body);
    QFont infoFont = infoLabel->font();
    infoFont.setBold(true);
    infoLabel->setFont(infoFont);
    centralLayout->addWidget(infoLabel);

    QTextBrowser *diffBrowser = new QTextBrowser(centralWidget);
    diffBrowser->setReadOnly(true);
    diffBrowser->setFrameShape(QFrame::NoFrame);
    diffBrowser->setStyleSheet(
        "QTextBrowser { background: #1e1e1e; color: #d4d4d4; "
        "font-family: 'Consolas', monospace; font-size: 13px; "
        "padding: 12px; border-radius: 8px; }");
    diffBrowser->setPlainText(diffList.join("\n\n--------------------\n\n"));
    centralLayout->addWidget(diffBrowser, 1);

    dlg.setCentralWidget(centralWidget);
    dlg.setLeftButtonText("拒绝修改");
    dlg.setRightButtonText("接受修改");

    QObject::connect(&dlg, &ElaContentDialog::leftButtonClicked, &dlg, &QDialog::reject);
    QObject::connect(&dlg, &ElaContentDialog::rightButtonClicked, &dlg, &QDialog::accept);

    // Enter/Return 快捷键直接确认（原来只有 Chat 模式的弹窗有这个快捷键，
    // Project 模式没有，合并后顺便统一成两边行为一致）
    QShortcut *enterShortcut = new QShortcut(QKeySequence(Qt::Key_Return), &dlg);
    QShortcut *enterShortcut2 = new QShortcut(QKeySequence(Qt::Key_Enter), &dlg);
    QObject::connect(enterShortcut, &QShortcut::activated, &dlg, &QDialog::accept);
    QObject::connect(enterShortcut2, &QShortcut::activated, &dlg, &QDialog::accept);

    return dlg.exec() == QDialog::Accepted;
}

} // namespace ConfirmDialogs
