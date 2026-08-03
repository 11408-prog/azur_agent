#include "project/activitypanel.h"
#include "ui/uiconstants.h"

#include <QScrollBar>
#include <QHBoxLayout>

ActivityPanel::ActivityPanel(QWidget *parent)
    : QWidget(parent)
    , layout_(nullptr)
    , contentWidget_(nullptr)
    , spacer_(nullptr)
    , scrollArea_(nullptr)
    , spinnerTimer_(nullptr)
{
    QVBoxLayout *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // MODIFIED: 滚动条改为半透明白色
    scrollArea_->setStyleSheet(
        "QScrollArea { background: transparent; }"
        "QScrollBar:vertical { background: transparent; width: 5px; margin: 0; }"
        "QScrollBar::handle:vertical { background: rgba(120, 120, 130, 0.3); border-radius: 3px; min-height: 24px; }"
        "QScrollBar::handle:vertical:hover { background: rgba(120, 120, 130, 0.5); }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
        );
    scrollArea_->viewport()->setStyleSheet("background: transparent;");

    contentWidget_ = new QWidget();
    contentWidget_->setStyleSheet("background: transparent;");
    layout_ = new QVBoxLayout(contentWidget_);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(6);

    spacer_ = new QWidget(contentWidget_);
    spacer_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout_->addWidget(spacer_);

    scrollArea_->setWidget(contentWidget_);
    outerLayout->addWidget(scrollArea_);

    spinnerTimer_ = new QTimer(this);
    spinnerTimer_->setInterval(200);
    connect(spinnerTimer_, &QTimer::timeout, this, [this]() {
        spinnerFrame_ = (spinnerFrame_ + 1) % UiConstants::kSpinnerFrames.size();
        for (int i = 0; i < rows_.size(); ++i) {
            if (i < entries_.size() && entries_[i].status == ActivityEntry::Pending) {
                if (rows_[i].icon) {
                    rows_[i].icon->setText(UiConstants::kSpinnerFrames[spinnerFrame_]);
                }
            }
        }
    });
}

void ActivityPanel::onStepChanged(const QString &text)
{
    if (text.startsWith(QStringLiteral("\u2713"))) {
        QString body = text.mid(1).trimmed();
        completeLastPending(body);
    } else if (text.startsWith(QStringLiteral("\u2717"))) {
        QString body = text.mid(1).trimmed();
        failLastPending(body);
    } else {
        addPendingActivity(text);
    }
}

void ActivityPanel::addPendingActivity(const QString &text)
{
    ActivityEntry entry;
    entry.status = ActivityEntry::Pending;
    entry.text = text;

    QWidget *row = new QWidget(contentWidget_);
    row->setStyleSheet("background: transparent;");
    QHBoxLayout *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(8, 4, 8, 4);
    rowLayout->setSpacing(8);

    QLabel *iconLabel = new QLabel(UiConstants::kSpinnerFrames[spinnerFrame_], row);
    iconLabel->setFixedWidth(20);
    // MODIFIED: Pending 图标改为暖棕色调
    iconLabel->setStyleSheet("color: #5a8ae6; font-size: 12px; background: transparent;");
    iconLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    QLabel *textLabel = new QLabel(text, row);
    // MODIFIED: 文字改为暖灰色
    textLabel->setStyleSheet("color: #4a4a5a; font-size: 12px; background: transparent;");
    textLabel->setWordWrap(true);
    textLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    rowLayout->addWidget(iconLabel);
    rowLayout->addWidget(textLabel, 1);

    layout_->insertWidget(layout_->count() - 1, row);

    entries_.append(entry);
    ActivityRow ar;
    ar.icon = iconLabel;
    ar.text = textLabel;
    rows_.append(ar);

    if (!spinnerTimer_->isActive()) {
        spinnerTimer_->start();
    }

    scrollToBottom();
}

void ActivityPanel::completeLastPending(const QString &finalText)
{
    for (int i = entries_.size() - 1; i >= 0; --i) {
        if (entries_[i].status == ActivityEntry::Pending) {
            entries_[i].status = ActivityEntry::Completed;
            entries_[i].text = finalText;

            if (i < rows_.size()) {
                if (rows_[i].icon) {
                    rows_[i].icon->setText(QStringLiteral("\u2713"));
                    // MODIFIED: 成功图标改为暖绿色
                    rows_[i].icon->setStyleSheet("color: #7aaa7a; font-size: 12px; background: transparent;");
                }
                if (rows_[i].text) {
                    rows_[i].text->setText(finalText);
                    // MODIFIED: 完成文字改为浅暖灰
                    rows_[i].text->setStyleSheet("color: #8a8a9a; font-size: 12px; background: transparent;");
                }
            }
            break;
        }
    }

    bool hasPending = false;
    for (const auto &e : qAsConst(entries_)) {
        if (e.status == ActivityEntry::Pending) { hasPending = true; break; }
    }
    if (!hasPending) {
        spinnerTimer_->stop();
    }

    scrollToBottom();
}

void ActivityPanel::failLastPending(const QString &finalText)
{
    for (int i = entries_.size() - 1; i >= 0; --i) {
        if (entries_[i].status == ActivityEntry::Pending) {
            entries_[i].status = ActivityEntry::Failed;
            entries_[i].text = finalText;

            if (i < rows_.size()) {
                if (rows_[i].icon) {
                    rows_[i].icon->setText(QStringLiteral("\u2717"));
                    // MODIFIED: 失败图标改为暖红色
                    rows_[i].icon->setStyleSheet("color: #d95555; font-size: 12px; background: transparent;");
                }
                if (rows_[i].text) {
                    rows_[i].text->setText(finalText);
                    // MODIFIED: 失败文字改为浅暖灰
                    rows_[i].text->setStyleSheet("color: #8a8a9a; font-size: 12px; background: transparent;");
                }
            }
            break;
        }
    }

    bool hasPending = false;
    for (const auto &e : qAsConst(entries_)) {
        if (e.status == ActivityEntry::Pending) { hasPending = true; break; }
    }
    if (!hasPending) {
        spinnerTimer_->stop();
    }

    scrollToBottom();
}

void ActivityPanel::clear()
{
    for (auto &row : rows_) {
        if (row.icon) {
            QWidget *parentRow = row.icon->parentWidget();
            if (parentRow) {
                layout_->removeWidget(parentRow);
                delete parentRow;
            }
        }
    }
    rows_.clear();
    entries_.clear();
    spinnerFrame_ = 0;
    spinnerTimer_->stop();
}

void ActivityPanel::scrollToBottom()
{
    if (scrollArea_) {
        QScrollBar *vBar = scrollArea_->verticalScrollBar();
        if (vBar) {
            vBar->setValue(vBar->maximum());
        }
    }
}