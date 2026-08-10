#ifndef MARKDOWNRENDERER_H
#define MARKDOWNRENDERER_H

#include <QString>

#include "ui/theme.h"

class QTextBrowser;

class MarkdownRenderer
{
public:
    // 返回 QTextDocument 默认样式表，随 dark 取色；配合 QTextBrowser::setMarkdown()
    // 渲染标题/引用/代码块/链接样式。dark 默认读 UiTheme::dark()，切换主题后
    // 重新设置默认样式表并重渲染即可刷新配色。
    static QString styleSheet(bool dark = UiTheme::dark());
    static void adjustTextBrowserHeight(QTextBrowser *browser);
};

#endif // MARKDOWNRENDERER_H
