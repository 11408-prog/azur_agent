#include "core/git_executor.h"

#include <QProcess>
#include <QDir>
#include <QJsonArray>
#include <QDebug>

// ==================== 通用 git 执行 ====================

QString GitExecutor::runGit(const QString &workspaceRoot, const QStringList &gitArgs,
                            bool *ok, QString *errorMsg)
{
    QProcess proc;
    proc.setWorkingDirectory(workspaceRoot);
    proc.start(QStringLiteral("git"), gitArgs);
    if (!proc.waitForStarted(5000)) {
        *ok = false;
        *errorMsg = QStringLiteral("无法启动 git 进程，请确认已安装 Git 并配置了 PATH");
        return {};
    }
    if (!proc.waitForFinished(30000)) {
        proc.kill();
        *ok = false;
        *errorMsg = QStringLiteral("git 命令执行超时（30 秒）");
        return {};
    }

    const QString stdoutStr = QString::fromUtf8(proc.readAllStandardOutput());
    const QString stderrStr = QString::fromUtf8(proc.readAllStandardError());
    const int exitCode = proc.exitCode();

    if (exitCode != 0) {
        *ok = false;
        *errorMsg = QStringLiteral("git 命令失败 (退出码 %1): %2")
            .arg(exitCode)
            .arg(stderrStr.trimmed());
        return {};
    }

    *ok = true;
    return stdoutStr;
}

// ==================== git status ====================

QString GitExecutor::status(const QString &workspaceRoot, const QJsonObject &args,
                            bool *ok, QString *displayLabel)
{
    Q_UNUSED(args);
    QString err;
    const QString output = runGit(workspaceRoot,
                                  {QStringLiteral("status"), QStringLiteral("--porcelain")},
                                  ok, &err);
    if (!*ok) {
        *displayLabel = QStringLiteral("git status 失败");
        return err;
    }

    // 解析 --porcelain 输出
    QStringList staged, unstaged, untracked;
    const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (line.length() < 3) continue;
        const QString xy = line.left(2);
        const QString path = line.mid(3).trimmed();
        // XY: 第一个字符是暂存区状态，第二个是工作区状态
        const QChar x = xy[0];
        const QChar y = xy[1];

        if (x != ' ' && x != '?') {
            staged << QStringLiteral("  %1  %2").arg(x).arg(path);
        }
        if (y != ' ' && y != '?') {
            unstaged << QStringLiteral("  %1  %2").arg(y).arg(path);
        }
        if (xy == QStringLiteral("??")) {
            untracked << QStringLiteral("  ?  %1").arg(path);
        }
    }

    QStringList result;
    result << QStringLiteral("=== Git Status ===");

    if (staged.isEmpty() && unstaged.isEmpty() && untracked.isEmpty()) {
        result << QStringLiteral("工作区干净，没有未提交的变更");
    } else {
        if (!staged.isEmpty()) {
            result << QStringLiteral("");
            result << QStringLiteral("【暂存区已变更】");
            result << QStringLiteral("  状态  文件");
            result << staged;
        }
        if (!unstaged.isEmpty()) {
            result << QStringLiteral("");
            result << QStringLiteral("【工作区未暂存】");
            result << QStringLiteral("  状态  文件");
            result << unstaged;
        }
        if (!untracked.isEmpty()) {
            result << QStringLiteral("");
            result << QStringLiteral("【未跟踪的新文件】");
            result << untracked;
        }
    }

    // 附加分支信息
    QString branchErr;
    bool branchOk = false;
    const QString branchOutput = runGit(workspaceRoot,
                                        {QStringLiteral("branch"), QStringLiteral("--show-current")},
                                        &branchOk, &branchErr);
    QString branchInfo;
    if (branchOk && !branchOutput.trimmed().isEmpty()) {
        branchInfo = QStringLiteral("当前分支: %1").arg(branchOutput.trimmed());
        result.insert(1, branchInfo);
    }

    const int changedCount = staged.size() + unstaged.size() + untracked.size();
    *displayLabel = QStringLiteral("git status (%1 个变更)").arg(changedCount);

    return result.join('\n');
}

// ==================== git diff ====================

QString GitExecutor::diff(const QString &workspaceRoot, const QJsonObject &args,
                          bool *ok, QString *displayLabel)
{
    const bool staged = args.value(QStringLiteral("staged")).toBool(false);
    const QJsonArray paths = args.value(QStringLiteral("paths")).toArray();

    QStringList gitArgs;
    gitArgs << QStringLiteral("diff");
    if (staged)
        gitArgs << QStringLiteral("--cached");

    // 收集路径参数
    QStringList pathList;
    for (const QJsonValue &v : paths) {
        const QString p = v.toString().trimmed();
        if (!p.isEmpty())
            pathList << p;
    }

    // 限制 --stat 摘要
    gitArgs << QStringLiteral("--");
    gitArgs << pathList;

    QString err;
    const QString output = runGit(workspaceRoot, gitArgs, ok, &err);
    if (!*ok) {
        *displayLabel = QStringLiteral("git diff 失败");
        return err;
    }

    if (output.trimmed().isEmpty()) {
        *ok = true;
        *displayLabel = QStringLiteral("git diff（无变更）");
        return QStringLiteral("=== Git Diff ===\n无变更");
    }

    // 解析 diff 输出，按文件分组
    QStringList result;
    result << (staged ? QStringLiteral("=== Git Diff (暂存区) ===")
                      : QStringLiteral("=== Git Diff (工作区) ==="));
    result << QStringLiteral("");

    const QStringList lines = output.split('\n');
    QString currentFile;
    int fileCount = 0;
    int totalAdd = 0, totalDel = 0;

    for (const QString &line : lines) {
        if (line.startsWith(QStringLiteral("diff --git a/"))) {
            // 提取文件名: "diff --git a/xxx b/xxx" → "xxx"
            const QString file = line.section(' ', 3).mid(3);
            if (!currentFile.isEmpty())
                result << QStringLiteral("");
            currentFile = file;
            result << QStringLiteral("--- a/%1").arg(currentFile)
                   << QStringLiteral("+++ b/%1").arg(currentFile);
            ++fileCount;
        } else if (line.startsWith(QStringLiteral("@@"))) {
            result << line;
        } else if (line.startsWith('+') && !line.startsWith(QStringLiteral("+++"))) {
            result << line;
            ++totalAdd;
        } else if (line.startsWith('-') && !line.startsWith(QStringLiteral("---"))) {
            result << line;
            ++totalDel;
        } else if (!line.startsWith(QStringLiteral("diff "))
                   && !line.startsWith(QStringLiteral("index "))
                   && !line.startsWith(QStringLiteral("new file"))
                   && !line.startsWith(QStringLiteral("deleted file"))
                   && !line.startsWith(QStringLiteral("old mode"))
                   && !line.startsWith(QStringLiteral("new mode"))) {
            // 上下文行也保留
            result << line;
        }
    }

    // 限制输出长度
    constexpr int kMaxDiffLines = 200;
    if (result.size() > kMaxDiffLines) {
        result = result.mid(0, kMaxDiffLines);
        result << QStringLiteral("");
        result << QStringLiteral("... (diff 过长，仅显示前 %1 行)").arg(kMaxDiffLines);
    }

    *displayLabel = QStringLiteral("git diff (%1 文件, +%2/-%3)")
        .arg(fileCount).arg(totalAdd).arg(totalDel);

    return result.join('\n');
}

// ==================== git log ====================

QString GitExecutor::log(const QString &workspaceRoot, const QJsonObject &args,
                         bool *ok, QString *displayLabel)
{
    const int maxCount = qBound(1, args.value(QStringLiteral("max_count")).toInt(10), 100);
    const QString branch = args.value(QStringLiteral("branch")).toString();

    QStringList gitArgs;
    gitArgs << QStringLiteral("log")
            << QStringLiteral("--oneline")
            << QStringLiteral("--abbrev-commit")
            << QString("--max-count=%1").arg(maxCount);

    if (!branch.isEmpty())
        gitArgs << branch;

    QString err;
    const QString output = runGit(workspaceRoot, gitArgs, ok, &err);
    if (!*ok) {
        *displayLabel = QStringLiteral("git log 失败");
        return err;
    }

    if (output.trimmed().isEmpty()) {
        *displayLabel = QStringLiteral("git log（无提交记录）");
        return QStringLiteral("=== Git Log ===\n没有提交记录");
    }

    QStringList result;
    result << QStringLiteral("=== Git Log (最近 %1 条) ===").arg(maxCount);
    result << QStringLiteral("");

    const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const int spaceIdx = line.indexOf(' ');
        if (spaceIdx > 0) {
            const QString hash = line.left(spaceIdx);
            const QString msg = line.mid(spaceIdx + 1);
            result << QStringLiteral("  %1  %2").arg(hash, msg);
        } else {
            result << QStringLiteral("  %1").arg(line);
        }
    }

    *displayLabel = QStringLiteral("git log (最近 %1 条)").arg(lines.size());
    return result.join('\n');
}

// ==================== git commit ====================

QString GitExecutor::commit(const QString &workspaceRoot, const QJsonObject &args,
                            bool *ok, QString *displayLabel)
{
    const QString message = args.value(QStringLiteral("message")).toString().trimmed();
    if (message.isEmpty()) {
        *ok = false;
        *displayLabel = QStringLiteral("git commit 失败");
        return QStringLiteral("错误：commit message 不能为空");
    }

    // 先暂存所有变更
    QString err;
    bool addOk = false;
    runGit(workspaceRoot, {QStringLiteral("add"), QStringLiteral("-A")}, &addOk, &err);
    if (!addOk) {
        *ok = false;
        *displayLabel = QStringLiteral("git add 失败");
        return QStringLiteral("错误：暂存变更时出错\n%1").arg(err);
    }

    // 执行 commit
    bool commitOk = false;
    const QString output = runGit(workspaceRoot,
                                  {QStringLiteral("commit"), QStringLiteral("-m"), message},
                                  &commitOk, &err);
    if (!commitOk) {
        if (err.contains(QStringLiteral("nothing to commit"), Qt::CaseInsensitive)) {
            *ok = true;
            *displayLabel = QStringLiteral("git commit（无变更可提交）");
            return QStringLiteral("=== Git Commit ===\n工作区没有需要提交的变更（已有提交内容或全部已是最新）");
        }
        *ok = false;
        *displayLabel = QStringLiteral("git commit 失败");
        return QStringLiteral("错误：提交失败\n%1").arg(err);
    }

    // 从输出中提取 commit hash
    QString hash;
    for (const QString &line : output.split('\n')) {
        if (line.contains(QStringLiteral("commit"))) {
            // 格式: "[master (root-commit) abc1234] message"
            // 或:   "[master abc1234] message"
            const int bracketEnd = line.indexOf(']');
            if (bracketEnd > 0) {
                const QString inside = line.mid(1, bracketEnd - 1);
                const QStringList parts = inside.split(' ');
                for (const QString &p : parts) {
                    if (p.length() >= 7 && !p.contains('(') && !p.contains(')')) {
                        hash = p;
                        break;
                    }
                }
            }
            break;
        }
    }

    *ok = true;
    *displayLabel = hash.isEmpty()
        ? QStringLiteral("git commit 成功")
        : QStringLiteral("git commit %1").arg(hash);

    QStringList result;
    result << QStringLiteral("=== Git Commit ===");
    if (!hash.isEmpty())
        result << QStringLiteral("提交哈希: %1").arg(hash);
    result << QStringLiteral("提交信息: %1").arg(message);
    result << QStringLiteral("状态: 提交成功");
    return result.join('\n');
}

// ==================== git branch ====================

QString GitExecutor::branch(const QString &workspaceRoot, const QJsonObject &args,
                            bool *ok, QString *displayLabel)
{
    Q_UNUSED(args);
    QString err;
    const QString output = runGit(workspaceRoot,
                                  {QStringLiteral("branch")},
                                  ok, &err);
    if (!*ok) {
        *displayLabel = QStringLiteral("git branch 失败");
        return err;
    }

    QStringList result;
    result << QStringLiteral("=== Git Branches ===");
    result << QStringLiteral("");

    const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    int branchCount = 0;
    QString currentBranch;
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            ++branchCount;
            if (line.startsWith('*')) {
                currentBranch = trimmed;
            }
            result << line;
        }
    }

    if (!currentBranch.isEmpty()) {
        *displayLabel = QStringLiteral("git branch (%1 分支, 当前: %2)")
            .arg(branchCount).arg(currentBranch);
    } else {
        *displayLabel = QStringLiteral("git branch (%1 分支)").arg(branchCount);
    }

    return result.join('\n');
}
