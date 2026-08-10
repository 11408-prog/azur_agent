#include "ui/messagebubblewidget.h"
#include "ui/markdownrenderer.h"
#include "ui/uiconstants.h"
#include "ui/theme.h"

#include <ElaText.h>
#include <ElaTheme.h>

#include <QLabel>
#include <QFrame>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextBrowser>
#include <QFont>
#include <QFile>
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
    QDir dir(QCoreApplication::applicationDirPath());
    while (!dir.exists() || !dir.exists("app/avatar/bot.png")) {
        if (!dir.cdUp()) break;
    }
    if (dir.exists("app/avatar/bot.png")) {
        return QDir::cleanPath(dir.absolutePath() + "/app/avatar");
    }
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
    QWidget *p = parentWidget();
    if (!p) return;
    int w = p->width() - 16;
    const int maxWidth = qMax(400, w - 40);
    bubble_->setMaximumWidth(qBound(400, w * 1 / 4, maxWidth));
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

    // 气泡容器改用普通 QFrame + WA_StyledBackground：
    // ElaScrollPageArea 的背景色只能来自全局主题色板，没法让用户气泡和 AI 气泡
    // 分别显示不同颜色；QFrame 打开 WA_StyledBackground 后，QSS 里写的
    // background/border/border-radius 才会真正按每个实例生效。
    bubble_ = new QFrame(this);
    bubble_->setObjectName(QStringLiteral("MessageBubbleFrame"));
    bubble_->setAttribute(Qt::WA_StyledBackground, true);
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
    contentBrowser_->setMinimumHeight(0);
    contentBrowser_->setVisible(false);

    connect(contentBrowser_, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        if (url.toString() == "azur://showall") {
            renderFullContent();
        } else {
            QDesktopServices::openUrl(url);
        }
    });
    bubbleLayout_->addWidget(contentBrowser_);

    timeLabel_ = new QLabel(bubble_);
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

    // 主题切换时重刷气泡 QSS + 已渲染的 Markdown
    connect(eTheme, &ElaTheme::themeModeChanged, this, [this](ElaThemeType::ThemeMode) {
        applyTheme();
    });
    applyTheme();
}

void MessageBubbleWidget::applyTheme()
{
    // 气泡：用户 = userBubbleBg，AI = aiBubbleBg；圆角 16→12，尾角 4，边框淡
    const QString bubbleQss = QString(
        "QFrame#MessageBubbleFrame {"
        "   background: %1;"
        "   border: 1px solid %2;"
        "   border-radius: 12px;"
        "   border-bottom-%3-radius: 4px;"
        "}"
        )
        .arg(UiTheme::qss(isUser_ ? UiTheme::userBubbleBg() : UiTheme::aiBubbleBg()),
             UiTheme::qss(UiTheme::border()),
             isUser_ ? QStringLiteral("right") : QStringLiteral("left"));
    bubble_->setStyleSheet(bubbleQss);

    // 内容区：正文 + 链接色（流式纯文本前景色也由这里控制）
    contentBrowser_->setStyleSheet(QString(
        "QTextBrowser { background: transparent; border: none; color: %1; }"
        "QTextBrowser a { color: %2; }"
        )
        .arg(UiTheme::qss(UiTheme::textPrimary()),
             UiTheme::qss(UiTheme::linkColor())));

    if (timeLabel_) {
        timeLabel_->setStyleSheet(
            QString("font-size: 11px; color: %1; background: transparent;")
                .arg(UiTheme::qss(UiTheme::textSecondary())));
    }

    // 步骤指示器：旋转中 = accent；已收尾 ✓/✗ 保留原来的 success/danger 色
    if (stepIcon_) {
        const QString t = stepIcon_->text();
        if (t != QStringLiteral("✓") && t != QStringLiteral("✗")) {
            stepIcon_->setStyleSheet(QString("color:%1; font-size:13px; background:transparent;")
                                         .arg(UiTheme::qss(UiTheme::accent())));
        }
    }
    if (stepText_) {
        stepText_->setStyleSheet(QString("color:%1; font-size:12px; background:transparent;")
                                     .arg(UiTheme::qss(UiTheme::textSecondary())));
    }

    // 头像占位（无图片时用 accent 底）
    if (avatar_) {
        const QPixmap pm = avatar_->pixmap(Qt::ReturnByValue);
        if (pm.isNull()) {
            avatar_->setStyleSheet(QStringLiteral(
                "background-color: %1; color: white;"
                "border-radius: 20px; font-weight: bold; font-size: 16px;"
                )
                .arg(UiTheme::qss(UiTheme::accent())));
        }
    }

    // 已渲染的 Markdown 重刷一遍（深色下颜色才会正确刷新）
    if (!currentMarkdown_.isEmpty()) {
        renderMarkdown(currentMarkdown_);
    }
}

QLabel *MessageBubbleWidget::createAvatar()
{
    QLabel *avatar = new QLabel(this);
    avatar->setFixedSize(40, 40);
    avatar->setAlignment(Qt::AlignCenter);

    if (s_avatarDir.isEmpty()) {
        s_avatarDir = resolveAvatarDir();
    }
    const QString avatarFile = s_avatarDir + "/" + (isUser_ ? "user.png" : "bot.png");
    qDebug() << "[Avatar] trying:" << avatarFile << "exists:" << QFile::exists(avatarFile)
             << "appDir:" << QCoreApplication::applicationDirPath();
    QPixmap pix(avatarFile);
    if (!pix.isNull()) {
        static constexpr int kAvatarSize = 40;
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
                                  "border-radius: 20px; font-weight: bold; font-size: 16px;"
                                  ).arg(UiTheme::qss(UiTheme::accent())));
    }
    return avatar;
}

void MessageBubbleWidget::setUserContent(const QString &text)
{
    contentBrowser_->setVisible(false);
    userText_->setText(text);
    userText_->setVisible(true);
}

void MessageBubbleWidget::setAiContent(const QString &markdown)
{
    currentMarkdown_ = markdown;
    renderMarkdown(markdown);
}

void MessageBubbleWidget::applyMarkdownTheme()
{
    contentBrowser_->document()->setDefaultStyleSheet(MarkdownRenderer::styleSheet(UiTheme::dark()));
    QFont f = contentBrowser_->font();
    f.setPixelSize(15);
    contentBrowser_->setFont(f);
}

void MessageBubbleWidget::renderMarkdown(const QString &markdown)
{
    userText_->setVisible(false);
    contentBrowser_->setVisible(true);

    applyMarkdownTheme();

    int lineCount = markdown.count('\n');
    if (lineCount > kLazyRenderThreshold) {
        fullMarkdown_ = markdown;

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
        preview += "\n\n---\n\n[显示全部内容](azur://showall)";

        contentBrowser_->setMarkdown(preview);
    } else {
        fullMarkdown_.clear();
        contentBrowser_->setMarkdown(markdown);
    }
    MarkdownRenderer::adjustTextBrowserHeight(contentBrowser_);
}

void MessageBubbleWidget::renderFullContent()
{
    if (fullMarkdown_.isEmpty()) return;
    applyMarkdownTheme();
    contentBrowser_->setMarkdown(fullMarkdown_);
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
    if (contentSpinnerTimer_ && contentSpinnerTimer_->isActive()) {
        contentSpinnerTimer_->stop();
    }
    currentMarkdown_.clear();   // 流式纯文本无需按 Markdown 重渲染
    userText_->setVisible(false);
    contentBrowser_->setVisible(true);
    contentBrowser_->setPlainText(plainText);
    MarkdownRenderer::adjustTextBrowserHeight(contentBrowser_);
    updateBubbleMaxWidth();
}

QTextBrowser *MessageBubbleWidget::aiContentBrowser() const
{
    return contentBrowser_;
}

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
    stepIcon_->setFixedWidth(16);
    stepIcon_->setStyleSheet(QString("color:%1; font-size:13px; background:transparent;")
                                 .arg(UiTheme::qss(UiTheme::accent())));
    stepText_ = new QLabel("正在连接 DeepSeek...", stepRow_);
    stepText_->setStyleSheet(QString("color:%1; font-size:12px; background:transparent;")
                                 .arg(UiTheme::qss(UiTheme::textSecondary())));

    stepRowLayout->addWidget(stepIcon_);
    stepRowLayout->addWidget(stepText_, 1);

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
        stepIcon_->setStyleSheet(QString("color:%1; font-size:13px; background:transparent;")
                                     .arg(UiTheme::qss(success ? UiTheme::success() : UiTheme::danger())));
    }
    if (stepText_) {
        stepText_->setText(finalText);
        stepText_->setStyleSheet(QString("color:%1; font-size:11px; background:transparent;")
                                     .arg(UiTheme::qss(UiTheme::textSecondary())));
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

void MessageBubbleWidget::setTimestamp(const QString &ts)
{
    timeLabel_->setText(ts);
}
