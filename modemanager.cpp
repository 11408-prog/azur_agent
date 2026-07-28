#include "modemanager.h"
#include "projectsession.h"
#include "projectpage.h"
#include "projectconversationservice.h"
#include "tool_executor.h"

#include <QDir>
#include <QFileDialog>
#include <QSettings>
#include <QTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>

ModeManager::ModeManager(QObject *parent)
    : QObject(parent)
{
}

ModeManager::~ModeManager()
{
    delete currentProject_;
}

// ==================== 模式切换 ====================

void ModeManager::enterProjectMode(QWidget *parentWidget)
{
    qDebug() << "[MODE] enterProjectMode | 当前模式=" << (currentMode_ == AgentMode::Chat ? "Chat" : "Project");
    if (currentMode_ == AgentMode::Project) return;
    currentMode_ = AgentMode::Project;

    // 如果还没有项目路径，弹窗让用户选择
    if (!currentProject_ || currentProject_->projectPath.isEmpty()) {
        QSettings s("AzurStudio", "AzurAgent");
        QString lastProject = s.value("lastProjectPath").toString();

        const QString dirPath = QFileDialog::getExistingDirectory(
            parentWidget, "选择项目目录",
            lastProject.isEmpty() ? QDir::homePath() : lastProject);
        if (dirPath.isEmpty()) {
            currentMode_ = AgentMode::Chat;      // 用户取消，退回 Chat 模式
            emit modeChanged(AgentMode::Chat);
            return;
        }

        currentProject_ = new ProjectSession(ProjectSession::load(dirPath));
        if (!currentProject_->isValid()) {
            delete currentProject_;
            currentProject_ = new ProjectSession();
            currentProject_->projectPath = dirPath;
            currentProject_->save();
        }
        s.setValue("lastProjectPath", dirPath);
    }

    // 延迟执行重任务，让 UI 先完成导航动画
    QTimer::singleShot(0, this, &ModeManager::finishProjectInit);
}

void ModeManager::enterChatMode()
{
    qDebug() << "[MODE] enterChatMode | 当前模式=" << (currentMode_ == AgentMode::Chat ? "Chat" : "Project");
    if (currentMode_ == AgentMode::Chat) return;
    currentMode_ = AgentMode::Chat;

    if (projectPageWidget_) {
        projectPageWidget_->setActive(false);
    }
    ToolExecutor::setAllowedPaths({});

    emit modeChanged(AgentMode::Chat);
}

void ModeManager::finishProjectInit()
{
    if (!currentProject_ || !projectConvService_ || !projectPageWidget_) return;

    const QString projPath = QDir::toNativeSeparators(QDir::cleanPath(currentProject_->projectPath));
    const QString prevConvId = ProjectConversationService::findEntryConversationId(projPath);
    currentConversationId_ = projectConvService_->resolveConversation(projPath, prevConvId);

    // 加载历史对话消息
    QJsonArray prevMessages = projectConvService_->loadConversation(currentConversationId_);
    QList<QJsonObject> msgList;
    for (const QJsonValue &v : prevMessages) msgList.append(v.toObject());
    projectPageWidget_->restoreConversation(msgList);

    // 初始化项目环境
    projectPageWidget_->loadSystemPrompt();
    projectPageWidget_->setActive(true);

    ToolExecutor::setAllowedPaths(currentProject_->allowedPaths);

    saveEntry();
    emit modeChanged(AgentMode::Project);
    emit projectPathChanged(currentProject_->projectPath);
    emit conversationIdChanged(currentConversationId_);

    qDebug() << "[MODE] finishProjectInit 完成 | convId=" << currentConversationId_;

    // 延迟执行索引重建，让 UI 先完成所有更新
    QTimer::singleShot(0, this, [this]() {
        if (currentProject_ && !currentProject_->projectPath.isEmpty() && projectPageWidget_) {
            projectPageWidget_->setProjectPath(currentProject_->projectPath);
        }
    });
}

// ==================== 项目对话管理 ====================

void ModeManager::saveConversation()
{
    if (currentMode_ != AgentMode::Project || !projectPageWidget_ || !projectConvService_) return;
    const QList<QJsonObject> messages = projectPageWidget_->conversation();
    if (messages.isEmpty()) return;
    QJsonArray arr;
    for (const auto &msg : messages) arr.append(msg);
    QString projectPath = currentProject_ ? currentProject_->projectPath : QString();
    projectConvService_->saveConversation(currentConversationId_, arr, projectPath);
}

void ModeManager::saveEntry()
{
    if (!currentProject_ || currentProject_->projectPath.isEmpty() || !projectConvService_) return;
    QString convTitle = projectConvService_->conversationTitle(currentConversationId_);
    ProjectConversationService::saveProjectEntry(
        currentProject_->projectPath, currentConversationId_, convTitle);
}

void ModeManager::switchToEntry(const QString &path, const QString &convId)
{
    if (!projectPageWidget_ || !projectConvService_) return;

    // 保存当前项目对话
    saveConversation();
    saveEntry();

    const QString cleanPath = QDir::toNativeSeparators(QDir::cleanPath(path));

    // 切换到目标项目
    delete currentProject_;
    currentProject_ = new ProjectSession(ProjectSession::load(cleanPath));
    if (!currentProject_->isValid()) {
        delete currentProject_;
        currentProject_ = new ProjectSession();
        currentProject_->projectPath = cleanPath;
        currentProject_->save();
    }

    currentConversationId_ = projectConvService_->resolveConversation(cleanPath, convId);

    // 更新 UI
    if (projectPageWidget_) {
        projectPageWidget_->setProjectPath(currentProject_->projectPath);
    }
    ToolExecutor::setAllowedPaths(currentProject_->allowedPaths);

    // 加载历史对话
    QJsonArray messages = projectConvService_->loadConversation(currentConversationId_);
    QList<QJsonObject> msgList;
    for (const QJsonValue &v : messages) msgList.append(v.toObject());
    projectPageWidget_->restoreConversation(msgList);

    saveEntry();
    emit projectPathChanged(currentProject_->projectPath);
    emit conversationIdChanged(currentConversationId_);
}

void ModeManager::switchToConversation(const QString &convId)
{
    if (!projectPageWidget_ || !projectConvService_) return;

    // 先保存当前对话
    saveConversation();

    // 切换到目标对话
    currentConversationId_ = convId;
    QJsonArray messages = projectConvService_->loadConversation(convId);
    QList<QJsonObject> msgList;
    for (const QJsonValue &v : messages) msgList.append(v.toObject());
    projectPageWidget_->restoreConversation(msgList);
    saveEntry();

    emit conversationIdChanged(currentConversationId_);
    qDebug() << "[MODE] 切换到项目对话:" << convId;
}

void ModeManager::openProject(const QString &dir)
{
    if (dir.isEmpty() || !projectConvService_ || !projectPageWidget_) return;

    // 创建新对话并绑定到新项目
    currentConversationId_ = projectConvService_->createConversation(dir);

    delete currentProject_;
    currentProject_ = new ProjectSession(ProjectSession::load(dir));
    if (!currentProject_->isValid()) {
        delete currentProject_;
        currentProject_ = new ProjectSession();
        currentProject_->projectPath = dir;
        currentProject_->save();
    }

    projectPageWidget_->setProjectPath(currentProject_->projectPath);
    ToolExecutor::setAllowedPaths(currentProject_->allowedPaths);

    projectPageWidget_->restoreConversation({});
    saveEntry();

    emit projectPathChanged(currentProject_->projectPath);
    emit conversationIdChanged(currentConversationId_);
}
