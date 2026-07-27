#ifndef MARKDOWNRENDERER_H
#define MARKDOWNRENDERER_H

#include <QString>
#include <QStringList>

class QTextBrowser;

class MarkdownRenderer
{
public:
    static QString toHtml(const QString &md, QStringList *rawCodeBlocksOut = nullptr);
    static void adjustTextBrowserHeight(QTextBrowser *browser);
};

#endif // MARKDOWNRENDERER_H
