#ifndef UI_THEME_H
#define UI_THEME_H

#include <QColor>
#include <QString>

#include <ElaTheme.h>

// 统一主题色板：从 Ela 的主题色板（ElaThemeType::ThemeColor）取当前 light/dark
// 模式下的颜色，并提供一组具名快捷色，供所有自定义 QSS / HTML 使用。
//
// 以前颜色散落在 chatpagewidget / conversationview / messagebubblewidget /
// markdownrenderer / settingpagewidget / mainwindow 里硬编码，切深色就花。
// 现在统一走这里：浅/深两套由 Ela 色板驱动，themeModeChanged 时各控件重刷。
//
// 这个头文件"即用"，全部是 inline 函数，无状态、无单例。
namespace UiTheme {

// 当前是否为深色模式（跟随 ElaTheme）
inline bool dark()
{
    return eTheme->getThemeMode() == ElaThemeType::Dark;
}

// 带显式模式的重载：供渲染器等需要在指定模式（而非当前模式）下取色的场景使用
inline QColor color(ElaThemeType::ThemeColor c, bool darkMode)
{
    return eTheme->getThemeColor(darkMode ? ElaThemeType::Dark : ElaThemeType::Light, c);
}

// 取当前模式下的 Ela 主题色
inline QColor color(ElaThemeType::ThemeColor c)
{
    return color(c, dark());
}

// 转成 QSS 可用的 rgba(...) 字符串。alpha 传 -1 表示用颜色自带的 alpha。
inline QString qss(const QColor &c, int alpha = -1)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(c.red())
        .arg(c.green())
        .arg(c.blue())
        .arg(alpha >= 0 ? alpha : c.alpha());
}

// 把 fg 以 alpha(0..1) 叠加到 bg 上，返回完全不透明的合成色
inline QColor over(const QColor &fg, const QColor &bg, qreal alpha)
{
    return QColor(
        qRound(fg.red() * alpha + bg.red() * (1.0 - alpha)),
        qRound(fg.green() * alpha + bg.green() * (1.0 - alpha)),
        qRound(fg.blue() * alpha + bg.blue() * (1.0 - alpha)));
}

// ---- 具名快捷色 ----
inline QColor accent() { return color(ElaThemeType::PrimaryNormal); }
inline QColor accentHover() { return color(ElaThemeType::PrimaryHover); }
inline QColor accentPress() { return color(ElaThemeType::PrimaryPress); }
inline QColor bg() { return color(ElaThemeType::WindowCentralStackBase); }
inline QColor panelBg() { return color(ElaThemeType::BasicBase); }
inline QColor surface() { return color(ElaThemeType::BasicBaseDeep); }
inline QColor border() { return color(ElaThemeType::BasicBorder); }
inline QColor textPrimary() { return color(ElaThemeType::BasicText); }
inline QColor textPrimaryFor(bool darkMode) { return color(ElaThemeType::BasicText, darkMode); }
inline QColor textSecondary() { return color(ElaThemeType::BasicDetailsText); }
inline QColor textOnAccent() { return color(ElaThemeType::BasicTextInvert); }

// 选中/悬停态的背景：accent 低透明度叠在面板色上
inline QColor accentOverlay(qreal alpha = -1.0)
{
    if (alpha < 0) alpha = dark() ? 0.28 : 0.14;
    return over(accent(), panelBg(), alpha);
}

// 中性 hover 背景
inline QColor hoverOverlay()
{
    const qreal a = dark() ? 0.12 : 0.06;
    return over(textPrimary(), panelBg(), a);
}

// 用户气泡：浅色 = accent 淡叠，深色 = accent 稍明显叠在深 surface 上
inline QColor userBubbleBg()
{
    const qreal a = dark() ? 0.30 : 0.12;
    return over(accent(), surface(), a);
}

// AI 气泡：surface + 边框（边框在 QSS 里加）
inline QColor aiBubbleBg() { return surface(); }

// 代码块：两种模式都保持暗底（惯例）
inline QColor codeBgFor(bool darkMode) { return darkMode ? QColor(0x0d, 0x11, 0x17) : QColor(0x23, 0x2a, 0x38); }
inline QColor codeBg() { return codeBgFor(dark()); }
inline QColor codeHeaderBgFor(bool darkMode) { return darkMode ? QColor(0x01, 0x04, 0x09) : QColor(0x17, 0x1d, 0x29); }
inline QColor codeHeaderBg() { return codeHeaderBgFor(dark()); }
inline QColor codeTextFor(bool) { return QColor(0xe2, 0xe8, 0xf0); }
inline QColor codeText() { return codeTextFor(dark()); }

// inline code
inline QColor inlineCodeBgFor(bool darkMode) { return darkMode ? QColor(0x2d, 0x33, 0x3f) : QColor(0xef, 0xf1, 0xf3); }
inline QColor inlineCodeBg() { return inlineCodeBgFor(dark()); }
inline QColor inlineCodeTextFor(bool darkMode) { return darkMode ? QColor(0xe6, 0xe6, 0xe6) : QColor(0x24, 0x29, 0x2e); }
inline QColor inlineCodeText() { return inlineCodeTextFor(dark()); }

// 链接色
inline QColor linkColorFor(bool darkMode) { return darkMode ? QColor(0x6e, 0xa8, 0xfe) : QColor(0x0f, 0x5f, 0xf0); }
inline QColor linkColor() { return linkColorFor(dark()); }

// 状态色（步骤指示器 / diff 等）
inline QColor success() { return dark() ? QColor(0x7a, 0xaa, 0x7a) : QColor(0x4c, 0x8a, 0x4c); }
inline QColor danger() { return dark() ? QColor(0xe0, 0x6a, 0x6a) : QColor(0xd9, 0x55, 0x55); }

} // namespace UiTheme

#endif // UI_THEME_H
