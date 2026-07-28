#include <QtTest>

#include "markdownrenderer.h"

class TstMarkdownRenderer : public QObject
{
    Q_OBJECT

private slots:
    void empty_returnsAsIs();
    void escapesHtmlSpecialChars();
    void bold_doubleAsterisk();
    void bold_doubleUnderscore();
    void italic_singleAsterisk();
    void inlineCode();
    void link();
    void newline_becomesBr();

    void codeBlock_extractsRawCodeAndBuildsCopyLink();
    void codeBlock_rawCodeIsUnescaped();
    void codeBlock_multipleBlocks_eachGetsOwnIndex();
    void codeBlock_withoutLanguage_labelsAsText();

    void diffCodeBlock_addedLinesColoredGreen();
    void diffCodeBlock_removedLinesColoredRed();
    void diffCodeBlock_headerLinesNotColored();

    void inlineMarkup_notAppliedInsideCodeBlock();
};

void TstMarkdownRenderer::empty_returnsAsIs()
{
    QCOMPARE(MarkdownRenderer::toHtml(QString()), QString());
    QCOMPARE(MarkdownRenderer::toHtml(QString("")), QString(""));
}

void TstMarkdownRenderer::escapesHtmlSpecialChars()
{
    const QString html = MarkdownRenderer::toHtml("<script>alert(1)</script> & more");
    QVERIFY(!html.contains("<script>"));
    QVERIFY(html.contains("&lt;script&gt;"));
    QVERIFY(html.contains("&amp;"));
}

void TstMarkdownRenderer::bold_doubleAsterisk()
{
    const QString html = MarkdownRenderer::toHtml("**重要**");
    QVERIFY2(html.contains("<b>重要</b>"), qPrintable(html));
}

void TstMarkdownRenderer::bold_doubleUnderscore()
{
    const QString html = MarkdownRenderer::toHtml("__重要__");
    QVERIFY2(html.contains("<b>重要</b>"), qPrintable(html));
}

void TstMarkdownRenderer::italic_singleAsterisk()
{
    const QString html = MarkdownRenderer::toHtml("*斜体*");
    QVERIFY2(html.contains("<i>斜体</i>"), qPrintable(html));
}

void TstMarkdownRenderer::inlineCode()
{
    const QString html = MarkdownRenderer::toHtml("使用 `qDebug()` 打印日志");
    QVERIFY2(html.contains("<code") && html.contains("qDebug()</code>"), qPrintable(html));
}

void TstMarkdownRenderer::link()
{
    const QString html = MarkdownRenderer::toHtml("参见 [文档](https://example.com/docs)");
    QVERIFY2(html.contains("<a href=\"https://example.com/docs\">文档</a>"), qPrintable(html));
}

void TstMarkdownRenderer::newline_becomesBr()
{
    const QString html = MarkdownRenderer::toHtml("第一行\n第二行");
    QVERIFY2(html.contains("第一行<br>第二行"), qPrintable(html));
}

void TstMarkdownRenderer::codeBlock_extractsRawCodeAndBuildsCopyLink()
{
    QStringList rawBlocks;
    const QString md = "说明文字\n```cpp\nint x = 1;\n```\n后续文字";
    const QString html = MarkdownRenderer::toHtml(md, &rawBlocks);

    QCOMPARE(rawBlocks.size(), 1);
    QCOMPARE(rawBlocks.at(0), QStringLiteral("int x = 1;"));

    // 复制按钮的 href 里应该带上对应的代码块索引 0
    QVERIFY2(html.contains("copycode:0"), qPrintable(html));
    // 代码块头部应显示语言名
    QVERIFY2(html.contains(">   cpp<"), qPrintable(html));
}

void TstMarkdownRenderer::codeBlock_rawCodeIsUnescaped()
{
    QStringList rawBlocks;
    const QString md = "```cpp\nif (a < b && b > c) {}\n```";
    MarkdownRenderer::toHtml(md, &rawBlocks);

    QCOMPARE(rawBlocks.size(), 1);
    // 复制到剪贴板的原始代码应该是未转义的 < > &，而不是 HTML 实体
    QCOMPARE(rawBlocks.at(0), QStringLiteral("if (a < b && b > c) {}"));
}

void TstMarkdownRenderer::codeBlock_multipleBlocks_eachGetsOwnIndex()
{
    QStringList rawBlocks;
    const QString md = "```py\nprint(1)\n```\n中间文字\n```py\nprint(2)\n```";
    const QString html = MarkdownRenderer::toHtml(md, &rawBlocks);

    QCOMPARE(rawBlocks.size(), 2);
    QCOMPARE(rawBlocks.at(0), QStringLiteral("print(1)"));
    QCOMPARE(rawBlocks.at(1), QStringLiteral("print(2)"));
    QVERIFY(html.contains("copycode:0"));
    QVERIFY(html.contains("copycode:1"));
}

void TstMarkdownRenderer::codeBlock_withoutLanguage_labelsAsText()
{
    const QString html = MarkdownRenderer::toHtml("```\nplain block\n```");
    QVERIFY2(html.contains(">   text<"), qPrintable(html));
}

void TstMarkdownRenderer::diffCodeBlock_addedLinesColoredGreen()
{
    const QString md = "```diff\n+ added line\n```";
    const QString html = MarkdownRenderer::toHtml(md);
    QVERIFY2(html.contains("color:#3fb950;") && html.contains("+ added line"), qPrintable(html));
}

void TstMarkdownRenderer::diffCodeBlock_removedLinesColoredRed()
{
    const QString md = "```diff\n- removed line\n```";
    const QString html = MarkdownRenderer::toHtml(md);
    QVERIFY2(html.contains("color:#f85149;") && html.contains("- removed line"), qPrintable(html));
}

void TstMarkdownRenderer::diffCodeBlock_headerLinesNotColored()
{
    // unified diff 的文件头 "+++ "/"--- " 不应该被当成新增/删除行染色
    const QString md = "```diff\n+++ b/file.txt\n--- a/file.txt\n```";
    const QString html = MarkdownRenderer::toHtml(md);
    QVERIFY2(!html.contains("color:#3fb950;") && !html.contains("color:#f85149;"), qPrintable(html));
    QVERIFY(html.contains("+++ b/file.txt"));
    QVERIFY(html.contains("--- a/file.txt"));
}

void TstMarkdownRenderer::inlineMarkup_notAppliedInsideCodeBlock()
{
    // 代码块内容在替换阶段被占位符保护，行内 ** / * / ` 语法不应该被二次解析
    QStringList rawBlocks;
    const QString md = "```py\na = 1 * 2\nb = \"**not bold**\"\n```";
    const QString html = MarkdownRenderer::toHtml(md, &rawBlocks);

    QVERIFY2(!html.contains("<b>not bold</b>"), qPrintable(html));
    QVERIFY(rawBlocks.at(0).contains("**not bold**"));
}

QTEST_APPLESS_MAIN(TstMarkdownRenderer)
#include "tst_markdown_renderer.moc"
