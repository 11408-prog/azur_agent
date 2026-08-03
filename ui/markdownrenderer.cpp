#include "ui/markdownrenderer.h"

#include <QTextBrowser>
#include <QRegularExpression>
#include <cmath>

QString MarkdownRenderer::toHtml(const QString &md, QStringList *rawCodeBlocksOut)
{
    if (md.isEmpty()) return md;
    if (rawCodeBlocksOut) rawCodeBlocksOut->clear();

    QString html = md;

    html.replace("&", "&amp;");
    html.replace("<", "&lt;");
    html.replace(">", "&gt;");

    QStringList codeBlocks;
    QRegularExpression codeBlockRx("```(\\w*)\\n([\\s\\S]*?)```");
    qsizetype offset = 0;
    while (true) {
        QRegularExpressionMatch m = codeBlockRx.match(html, offset);
        if (!m.hasMatch()) break;

        QString lang = m.captured(1).trimmed();
        QString escapedCode = m.captured(2);
        while (escapedCode.endsWith('\n')) {
            escapedCode.chop(1);
        }

        QString rawCode = escapedCode;
        rawCode.replace("&amp;", "&");
        rawCode.replace("&lt;", "<");
        rawCode.replace("&gt;", ">");

        QString bodyHtml = escapedCode;
        if (lang.compare("diff", Qt::CaseInsensitive) == 0) {
            const QStringList lines = escapedCode.split('\n');
            QStringList colored;
            colored.reserve(lines.size());
            for (const QString &line : lines) {
                if (line.startsWith('+') && !line.startsWith("+++")) {
                    colored << QString("<span style=\"color:#5a9a5a;\">%1</span>").arg(line);
                } else if (line.startsWith('-') && !line.startsWith("---")) {
                    colored << QString("<span style=\"color:#d95555;\">%1</span>").arg(line);
                } else {
                    colored << line;
                }
            }
            bodyHtml = colored.join('\n');
        }

        int idx = codeBlocks.size();
        QString headerLabel = lang.isEmpty() ? QStringLiteral("text") : lang;
        // MODIFIED: 代码块改为暖色调暗底，与整体主题协调
        QString block = QString(
                            "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\" "
                            "style=\"background:#232a38; border:1px solid rgba(255,255,255,0.08); "
                            "border-radius:10px; margin:8px 0; overflow:hidden;\">"
                            "<tr>"
                            "<td style=\"padding:6px 14px; background:#171d29;\">"
                            "<span style=\"color:#5a8ae6; font-size:11px; letter-spacing:0.5px;\">%1</span>"
                            "</td>"
                            "<td align=\"right\" style=\"padding:6px 14px; background:#171d29;\">"
                            "<a href=\"copycode:%2\" style=\"color:#5a8ae6; font-size:11px; text-decoration:none;\">复制</a>"
                            "</td>"
                            "</tr>"
                            "<tr><td colspan=\"2\" style=\"padding:12px 16px;\">"
                            "<pre style=\"margin:0; color:#e2e8f0; font-family:'JetBrains Mono','Cascadia Code',monospace; "
                            "font-size:13px; line-height:1.6; white-space:pre-wrap;\"><code>%3</code></pre>"
                            "</td></tr>"
                            "</table>"
                            ).arg(headerLabel, QString::number(idx), bodyHtml);
        codeBlocks.append(block);
        if (rawCodeBlocksOut) {
            rawCodeBlocksOut->append(rawCode);
        }

        QString placeholder = QStringLiteral("\x01" "CB%1\x01").arg(idx);
        html.replace(m.capturedStart(), m.capturedLength(), placeholder);
        offset = m.capturedStart() + placeholder.length();
    }

    html.replace(QRegularExpression("`([^`]+)`"),
                 "<code style=\"background:#eff1f3; color:#24292e; padding:2px 6px; border-radius:4px; font-size:12.5px;\">\\1</code>");
    html.replace(QRegularExpression("\\*\\*(.+?)\\*\\*"), "<b>\\1</b>");
    html.replace(QRegularExpression("__(.+?)__"), "<b>\\1</b>");
    html.replace(QRegularExpression("\\*(.+?)\\*"), "<i>\\1</i>");
    html.replace(QRegularExpression("_(.+?)_"), "<i>\\1</i>");
    html.replace(QRegularExpression("\\[([^\\]]+)\\]\\(([^)]+)\\)"), "<a href=\"\\2\">\\1</a>");
    html.replace("\n", "<br>");

    for (int i = 0; i < codeBlocks.size(); ++i) {
        html.replace(QString("\x01" "CB%1\x01").arg(i), codeBlocks[i]);
    }

    return "<div style='line-height:1.6;'>" + html + "</div>";
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