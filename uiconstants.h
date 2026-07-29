#ifndef UI_CONSTANTS_H
#define UI_CONSTANTS_H

#include <QStringList>

// 项目里跨文件共用的一些小的 UI 常量。
//
// 目前只有一份：Agent"思考中/执行中"时用的旋转动画帧（盲文字符拼的 spinner）。
// 之前这个数组在 activitypanel.cpp / chatpagewidget.cpp / messagebubblewidget.cpp
// 里各自复制了一份（一共 4 处），其中 activitypanel.cpp 那份还在复制的时候漏掉了
// 一帧（少了 "⠇"），导致那边的 spinner 转起来跟其它地方节奏不完全一样——
// 这种不容易一眼看出来的小分叉，就是复制粘贴代码的典型代价，统一成一份之后不会再发生。
//
// C++17 的 inline 变量：允许在头文件里直接定义，被多个 .cpp 包含也不会报"重复定义"。
namespace UiConstants {

inline const QStringList kSpinnerFrames = {
    QStringLiteral("⠋"), QStringLiteral("⠙"), QStringLiteral("⠹"), QStringLiteral("⠸"),
    QStringLiteral("⠼"), QStringLiteral("⠴"), QStringLiteral("⠦"), QStringLiteral("⠧"),
    QStringLiteral("⠇"), QStringLiteral("⠏"),
};

} // namespace UiConstants

#endif // UI_CONSTANTS_H
