#include "projectsession.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>

QJsonObject ProjectSession::toJson() const
{
    qDebug()<<"[PROJ_SESS] toJson | projectPath="<<projectPath;
    QJsonObject obj;
    obj["projectPath"] = projectPath;
    QJsonArray paths;
    for (const QString &p : allowedPaths) {
        paths.append(QDir::toNativeSeparators(QDir::cleanPath(p)));
    }
    obj["allowedPaths"] = paths;
    return obj;
}

ProjectSession ProjectSession::fromJson(const QJsonObject &obj)
{
    qDebug()<<"[PROJ_SESS] fromJson | projectPath="<<obj["projectPath"].toString();
    ProjectSession session;
    session.projectPath = obj["projectPath"].toString();
    const QJsonArray paths = obj["allowedPaths"].toArray();
    for (const QJsonValue &v : paths) {
        const QString p = v.toString().trimmed();
        if (!p.isEmpty()) {
            session.allowedPaths.append(p);
        }
    }
    return session;
}

bool ProjectSession::save() const
{
    qDebug()<<"[PROJ_SESS] save | projectPath="<<projectPath;
    if (projectPath.isEmpty()) return false;
    QDir dir(projectPath + "/.azur");
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    QFile file(dir.filePath("project.json"));
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning()<<"[PROJ_SESS] 无法写入 project.json";
        return false;
    }
    file.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    return true;
}

ProjectSession ProjectSession::load(const QString &projectPath)
{
    qDebug()<<"[PROJ_SESS] load | projectPath="<<projectPath;
    QFile file(projectPath + "/.azur/project.json");
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug()<<"[PROJ_SESS] 未找到 project.json，返回空会话";
        return ProjectSession();
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isNull() || !doc.isObject()) return ProjectSession();
    return fromJson(doc.object());
}
