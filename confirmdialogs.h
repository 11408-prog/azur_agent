#ifndef CONFIRMDIALOGS_H
#define CONFIRMDIALOGS_H

#include <QStringList>

class QWidget;

// 通用确认弹窗集合。
//
// 原来 mainwindow.cpp（Chat 模式）和 projectpage.cpp（Project 模式）里
// 各自写了一份几乎一模一样的"写操作确认"弹窗代码（~40 行 diff 只有 0），
// 现在统一抽到这里，两边共用同一份实现，以后要改样式/交互只需要改一处。
namespace ConfirmDialogs {

// 展示 AI 写操作（write_file / apply_patch / run_command）的 diff 预览，
// 让用户选择"接受修改"或"拒绝修改"。
//   parent   : 弹窗的父窗口
//   diffList : 每个写操作对应的 diff/预览文本（由 ToolExecutor::previewDiff 生成）
// 返回值：true 表示用户点击了"接受修改"（或按下 Enter），false 表示拒绝/关闭弹窗。
bool confirmWriteOperations(QWidget *parent, const QStringList &diffList);

} // namespace ConfirmDialogs

#endif // CONFIRMDIALOGS_H
