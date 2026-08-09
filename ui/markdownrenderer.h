#ifndef MARKDOWNRENDERER_H
#define MARKDOWNRENDERER_H

#include <QString>
#include <QStringList>

#include "ui/theme.h"

class QTextBrowser;

class MarkdownRenderer
{
public:
    // dark 默认读 UiTheme::dark()，调用方无需显式传入；切换主题后重新调用即可刷新配色。
    static QString toHtml(const QString &md, QStringList *rawCodeBlocksOut = nullptr,
                          bool dark = UiTheme::dark());
    static void adjustTextBrowserHeight(QTextBrowser *browser);
};

#endif // MARKDOWNRENDERER_H
