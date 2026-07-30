#ifndef GIT_EXECUTOR_H
#define GIT_EXECUTOR_H

#include <QString>
#include <QJsonObject>

// GitExecutor：封装 Git 相关的工具调用。
// 所有方法均为静态，由 ToolExecutor::execute() 路由调用。
class GitExecutor
{
public:
    // git status：显示工作区和暂存区状态
    static QString status(const QString &workspaceRoot, const QJsonObject &args,
                          bool *ok, QString *displayLabel);

    // git diff：显示工作区或暂存区的变更
    static QString diff(const QString &workspaceRoot, const QJsonObject &args,
                        bool *ok, QString *displayLabel);

    // git log：查看提交历史
    static QString log(const QString &workspaceRoot, const QJsonObject &args,
                       bool *ok, QString *displayLabel);

    // git commit：暂存所有变更并提交
    static QString commit(const QString &workspaceRoot, const QJsonObject &args,
                          bool *ok, QString *displayLabel);

    // git branch：列出本地分支
    static QString branch(const QString &workspaceRoot, const QJsonObject &args,
                          bool *ok, QString *displayLabel);

private:
    // 在指定目录下执行 git 命令，返回 stdout
    static QString runGit(const QString &workspaceRoot, const QStringList &gitArgs,
                          bool *ok, QString *errorMsg);
};

#endif // GIT_EXECUTOR_H
