#include "messagebubblewidget.h"
#include "markdownrenderer.h"
#include "uiconstants.h"

#include <ElaScrollPageArea.h>
#include <ElaText.h>
#include <ElaMessageBar.h>

#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextBrowser>
#include <QFile>
#include <QGuiApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QUrl>
#include <QDateTime>
#include <QTimer>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>

#include <QCoreApplication>
#include <QDir>

static QString resolveAvatarDir()
{
    // 从 exe 目录开始，逐级向上查找 avatar/ 目录
    QDir dir(QCoreApplication::applicationDirPath());
    while (!dir.exists() || !dir.exists("avatar/bot.png")) {
        if (!dir.cdUp()) break;
    }
    if (dir.exists("avatar/bot.png")) {
        return QDir::cleanPath(dir.absolutePath() + "/avatar");
    }
    // fallback: 相对于 exe 目录
    return QCoreApplication::applicationDirPath() + "/avatar";
}

QString MessageBubbleWidget::s_avatarDir;

MessageBubbleWidget::MessageBubbleWidget(bool isUser, QWidget *parent)
    : QWidget(parent)
    , isUser_(isUser)
    , avatar_(nullptr)
    , bubble_(nullptr)
    , bubbleLayout_(nullptr)
    , contentBrowser_(nullptr)
    , userText_(nullptr)
    , timeLabel_(nullptr)
{
    initUI();
}

MessageBubbleWidget::~MessageBubbleWidget() = default;

void MessageBubbleWidget::setAvatarDirectory(const QString &dir)
{
    s_avatarDir = dir;
}

void MessageBubbleWidget::updateBubbleMaxWidth()
{
    // 自适应宽度：不超过父容器可用宽度的 3/4
    QWidget *p = parentWidget();
    if (!p) return;
    int w = p->width() - 16; // 减去边距
    bubble_->setMaximumWidth(qBound(400, w * 1 / 4, w - 40));
}

void MessageBubbleWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateBubbleMaxWidth();
}

void MessageBubbleWidget::initUI()
{
    QHBoxLayout *rowLayout = new QHBoxLayout(this);
    rowLayout->setContentsMargins(0, 4, 0, 4);
    rowLayout->setSpacing(8);

    avatar_ = createAvatar();

    bubble_ = new ElaScrollPageArea(this);
    bubble_->setBorderRadius(10);
    bubble_->setMinimumHeight(0);
    bubble_->setMaximumHeight(QWIDGETSIZE_MAX);
    bubble_->setMaximumWidth(700); // 初始值，resizeEvent 会动态更新

    bubbleLayout_ = new QVBoxLayout(bubble_);
    bubbleLayout_->setContentsMargins(12, 6, 12, 6);
    bubbleLayout_->setSpacing(3);

    userText_ = new ElaText(bubble_);
    userText_->setTextStyle(ElaTextType::Body);
    userText_->setWordWrap(true);
    userText_->setVisible(false);
    bubbleLayout_->addWidget(userText_);

    contentBrowser_ = new QTextBrowser(bubble_);
    contentBrowser_->setMinimumHeight(0);
    contentBrowser_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    contentBrowser_->setReadOnly(true);
    contentBrowser_->setFrameShape(QFrame::NoFrame);
    contentBrowser_->setOpenLinks(false);
    contentBrowser_->setOpenExternalLinks(false);
    contentBrowser_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    contentBrowser_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    contentBrowser_->setStyleSheet(
        "QTextBrowser { background: transparent; border: none; }"
        "QTextBrowser a { color: #4a9eff; }"
        );
    contentBrowser_->setMinimumHeight(0);
    contentBrowser_->setVisible(false);

    connect(contentBrowser_, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        const QString link = url.toString();
        if (link == "azur://showall") {
            renderFullContent();
        } else if (link.startsWith("copycode:")) {
            bool ok = false;
            int idx = link.mid(9).toInt(&ok);
            const QStringList blocks = contentBrowser_->property("codeBlocks").toStringList();
            if (ok && idx >= 0 && idx < blocks.size()) {
                QGuiApplication::clipboard()->setText(blocks.at(idx));
                ElaMessageBar::success(ElaMessageBarType::TopRight, "已复制", "代码已复制到剪贴板", 1500);
            }
        } else {
            QDesktopServices::openUrl(url);
        }
    });
    bubbleLayout_->addWidget(contentBrowser_);

    timeLabel_ = new QLabel(bubble_);
    timeLabel_->setStyleSheet("font-size: 11px; color: #999; background: transparent;");
    timeLabel_->setAlignment(isUser_ ? Qt::AlignRight : Qt::AlignLeft);
    bubbleLayout_->addWidget(timeLabel_);

    if (isUser_) {
        rowLayout->addStretch();
        rowLayout->addWidget(bubble_, 0, Qt::AlignTop);
        rowLayout->addWidget(avatar_, 0, Qt::AlignTop);
    } else {
        rowLayout->addWidget(avatar_, 0, Qt::AlignTop);
        rowLayout->addWidget(bubble_, 0, Qt::AlignTop);
        rowLayout->addStretch();
    }
}

QLabel *MessageBubbleWidget::createAvatar()
{
    QLabel *avatar = new QLabel(this);
    avatar->setFixedSize(44, 44);
    avatar->setAlignment(Qt::AlignCenter);

    // 运行时惰性解析头像目录（避免静态初始化时 QCoreApplication 未创建）
    if (s_avatarDir.isEmpty()) {
        s_avatarDir = resolveAvatarDir();
    }
    const QString avatarFile = s_avatarDir + "/" + (isUser_ ? "user.png" : "bot.png");
    qDebug() << "[Avatar] trying:" << avatarFile << "exists:" << QFile::exists(avatarFile)
             << "appDir:" << QCoreApplication::applicationDirPath();
    QPixmap pix(avatarFile);
    if (!pix.isNull()) {
        // 2x 渲染保证 HiDPI 清晰度
        static constexpr int kAvatarSize = 44;
        static constexpr int kRenderScale = 2;
        int renderSize = kAvatarSize * kRenderScale;

        QPixmap rounded(renderSize, renderSize);
        rounded.fill(Qt::transparent);

        QPainter painter(&rounded);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        QPainterPath path;
        path.addEllipse(0, 0, renderSize, renderSize);
        painter.setClipPath(path);
        painter.drawPixmap(0, 0, renderSize, renderSize,
                           pix.scaled(renderSize, renderSize,
                                      Qt::KeepAspectRatio, Qt::SmoothTransformation));
        painter.end();
        rounded.setDevicePixelRatio(kRenderScale);
        avatar->setPixmap(rounded);
    } else {
        avatar->setText(isUser_ ? "U" : "E");
        avatar->setStyleSheet(QStringLiteral(
            "background-color: %1; color: white;"
            "border-radius: 22px; font-weight: bold; font-size: 17px;"
        ).arg(isUser_ ? "#4a9eff" : "#7c4dff"));
    }
    return avatar;
}

// ==================== 用户消息内容 ====================
void MessageBubbleWidget::setUserContent(const QString &text)
{
    contentBrowser_->setVisible(false);
    userText_->setText(text);
    userText_->setVisible(true);
}

// ==================== AI 消息内容 ====================
void MessageBubbleWidget::setAiContent(const QString &markdown)
{
    userText_->setVisible(false);
    contentBrowser_->setVisible(true);

    // 惰性渲染：超过阈值行数只渲染前 N 行
    int lineCount = markdown.count('\n');
    if (lineCount > kLazyRenderThreshold) {
        fullMarkdown_ = markdown;

        // 取前 kLazyRenderInitialLines 行
        int endPos = 0;
        int lines = 0;
        for (int i = 0; i < markdown.size(); ++i) {
            if (markdown[i] == '\n') {
                ++lines;
                if (lines >= kLazyRenderInitialLines) {
                    endPos = i;
                    break;
                }
            }
        }
        QString preview = (endPos > 0) ? markdown.left(endPos) : markdown;
        preview += "\n\n---\n\n<a href=\"azur://showall\" style=\"color: #4a9eff; text-decoration: none;\">"
                   "[显示全部内容]</a>";

        QStringList codeBlocks;
        contentBrowser_->setHtml(MarkdownRenderer::toHtml(preview, &codeBlocks));
        contentBrowser_->setProperty("codeBlocks", codeBlocks);
    } else {
        fullMarkdown_.clear();
        QStringList codeBlocks;
        contentBrowser_->setHtml(MarkdownRenderer::toHtml(markdown, &codeBlocks));
        contentBrowser_->setProperty("codeBlocks", codeBlocks);
    }
    MarkdownRenderer::adjustTextBrowserHeight(contentBrowser_);
}

void MessageBubbleWidget::renderFullContent()
{
    if (fullMarkdown_.isEmpty()) return;
    QStringList codeBlocks;
    contentBrowser_->setHtml(MarkdownRenderer::toHtml(fullMarkdown_, &codeBlocks));
    contentBrowser_->setProperty("codeBlocks", codeBlocks);
    fullMarkdown_.clear();
    MarkdownRenderer::adjustTextBrowserHeight(contentBrowser_);
}

void MessageBubbleWidget::startContentSpinner()
{
    if (!contentSpinnerTimer_) {
        contentSpinnerTimer_ = new QTimer(this);
        connect(contentSpinnerTimer_, &QTimer::timeout, this, [this]() {
            const QStringList &frames = UiConstants::kSpinnerFrames;
            contentSpinnerFrame_++;
            contentBrowser_->setPlainText(QStringLiteral("思考中 %1").arg(frames[contentSpinnerFrame_ % frames.size()]));
            MarkdownRenderer::adjustTextBrowserHeight(contentBrowser_);
        });
    }
    contentSpinnerFrame_ = 0;
    contentSpinnerTimer_->start(90);
}

void MessageBubbleWidget::stopContentSpinner()
{
    if (contentSpinnerTimer_) contentSpinnerTimer_->stop();
}

void MessageBubbleWidget::setAiStreamingContent(const QString &plainText)
{
    // 收到真正的内容时停止旋转动画
    if (contentSpinnerTimer_ && contentSpinnerTimer_->isActive()) {
        contentSpinnerTimer_->stop();
    }
    userText_->setVisible(false);
    contentBrowser_->setVisible(true);
    contentBrowser_->setPlainText(plainText);
    MarkdownRenderer::adjustTextBrowserHeight(contentBrowser_);
    // 更新最大宽度限制，确保内容完整可见
    updateBubbleMaxWidth();
}

QTextBrowser *MessageBubbleWidget::aiContentBrowser() const
{
    return contentBrowser_;
}

// ==================== 步骤指示器 ====================
void MessageBubbleWidget::enableStepIndicator(bool enable)
{
    stepIndicatorEnabled_ = enable;
    if (enable && !stepRow_) {
        createStepIndicator();
    } else if (!enable && stepRow_) {
        stepRow_->setVisible(false);
    }
}

bool MessageBubbleWidget::isStepIndicatorEnabled() const
{
    return stepIndicatorEnabled_;
}

void MessageBubbleWidget::createStepIndicator()
{
    stepRow_ = new QWidget(bubble_);
    QHBoxLayout *stepRowLayout = new QHBoxLayout(stepRow_);
    stepRowLayout->setContentsMargins(0, 0, 0, 2);
    stepRowLayout->setSpacing(6);

    stepIcon_ = new QLabel(stepRow_);
    stepIcon_->setStyleSheet("color:#4a9eff; font-size:13px; background:transparent;");
    stepIcon_->setFixedWidth(16);
    stepText_ = new QLabel("正在连接 DeepSeek...", stepRow_);
    stepText_->setStyleSheet("color:#888; font-size:12px; background:transparent;");

    stepRowLayout->addWidget(stepIcon_);
    stepRowLayout->addWidget(stepText_, 1);

    // 插入到 contentBrowser_ 之前
    bubbleLayout_->insertWidget(bubbleLayout_->count() - 1, stepRow_);
}

void MessageBubbleWidget::updateStep(const QString &text)
{
    if (stepText_) stepText_->setText(text);
}

void MessageBubbleWidget::finishStep(bool success, const QString &finalText)
{
    if (!stepRow_) return;
    if (stepIcon_) {
        stepIcon_->setText(success ? QStringLiteral("✓") : QStringLiteral("✗"));
        stepIcon_->setStyleSheet(success
                                    ? "color:#3fb950; font-size:13px; background:transparent;"
                                    : "color:#f85149; font-size:13px; background:transparent;");
    }
    if (stepText_) {
        stepText_->setText(finalText);
        stepText_->setStyleSheet("color:#888; font-size:11px; background:transparent;");
    }
    if (success) {
        stepRow_->setVisible(false);
    }
}

void MessageBubbleWidget::spinnerTick(int frame)
{
    if (stepIcon_) {
        const QStringList &frames = UiConstants::kSpinnerFrames;
        stepIcon_->setText(frames[frame % frames.size()]);
    }
}

// ==================== 时间戳 ====================
void MessageBubbleWidget::setTimestamp(const QString &ts)
{
    timeLabel_->setText(ts);
}
