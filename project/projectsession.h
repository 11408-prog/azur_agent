#ifndef PROJECTSESSION_H
#define PROJECTSESSION_H

#include <QString>
#include <QStringList>
#include <QJsonObject>

// 项目会话数据，保存在 {projectPath}/.azur/project.json
class ProjectSession
{
public:
    QString projectPath;
    QStringList allowedPaths;   // 额外允许访问的路径白名单

    QJsonObject toJson() const;
    static ProjectSession fromJson(const QJsonObject &obj);

    bool save() const;
    static ProjectSession load(const QString &projectPath);

    bool isValid() const { return !projectPath.isEmpty(); }
};

#endif // PROJECTSESSION_H
