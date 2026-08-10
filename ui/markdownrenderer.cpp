#include "ui/markdownrenderer.h"

#include <QTextBrowser>
#include <cmath>

QString MarkdownRenderer::styleSheet(bool dark)
{
    const QString textSecondary = UiTheme::color(ElaThemeType::BasicDetailsText, dark).name();
    const QString codeBg = UiTheme::codeBgFor(dark).name();
    const QString codeText = UiTheme::codeTextFor(dark).name();
    const QString inlineBg = UiTheme::inlineCodeBgFor(dark).name();
    const QString inlineText = UiTheme::inlineCodeTextFor(dark).name();
    const QString linkColor = UiTheme::linkColorFor(dark).name();

    return QString(
        "h1 { font-size: 22px; font-weight: bold; }\n"
        "h2 { font-size: 18px; font-weight: bold; }\n"
        "h3 { font-size: 16px; font-weight: bold; }\n"
        "h4, h5, h6 { font-size: 15px; font-weight: bold; }\n"
        "blockquote { color: %1; }\n"
        "pre { background-color: %2; color: %3; font-size: 13.5px; line-height: 1.6; }\n"
        "code { background-color: %4; color: %5; font-size: 13.5px; }\n"
        "a { color: %6; }\n"
        "code, pre { font-family: 'JetBrains Mono','Cascadia Code',monospace; }\n"
        ).arg(textSecondary, codeBg, codeText, inlineBg, inlineText, linkColor);
}

void MarkdownRenderer::adjustTextBrowserHeight(QTextBrowser *browser)
{
    if (!browser) return;

    const int fixedWidth = 380;
    browser->document()->setTextWidth(fixedWidth);
    browser->document()->setDocumentMargin(0);

    int height = static_cast<int>(std::ceil(browser->document()->size().height()));
    browser->setFixedHeight(height > 0 ? height + 1 : 0);
}