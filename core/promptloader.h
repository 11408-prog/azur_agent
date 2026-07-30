#ifndef PROMPTLOADER_H
#define PROMPTLOADER_H

#include <QString>

class PromptLoader
{
public:
    static QString loadFile(const QString &filename);
    static QString buildSystemPrompt();
};

#endif // PROMPTLOADER_H
