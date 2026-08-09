#include<ElaWindow.h>
#include <ElaApplication.h>

#include "app/mainwindow.h"

#include<QApplication>
#include<QDateTime>
#include<QFile>
#include<QTextStream>
#include<QDebug>
#include<QDir>
#include<QIcon>

//计算日志文件存放路径
QString getLogFilePath()
{
    //获取可执行文件所在目录
    QString appDir=QCoreApplication::applicationDirPath();
    //回溯至agent_根目录
    QString projectRoot = QDir::cleanPath(appDir+"/../..");
    //创建文件夹
    //定义日志目录
    QString logDir=projectRoot+"/daily_log";
    QDir dir;
    if(!dir.exists(logDir))
    {
        dir.mkpath(logDir);
    }
    //按日期生成日志文件名
    QString dateStr=QDateTime::currentDateTime().toString("yyyy-MM-dd");
    return logDir+"/azur_debug_"+dateStr+".log";
}
//将所有调试信息写入文件
void logToFile(QtMsgType type,const QMessageLogContext &context,const QString &msg)
{
    QString filePath=getLogFilePath();
    QFile file(filePath);
    if (file.open(QIODevice::Append|QIODevice::Text))
    {
        QTextStream out (&file);
        out<<QDateTime::currentDateTime().toString("hh:mm:ss.zzz")<<" ";
        switch(type)
        {
        case QtDebugMsg:out<<"[DBG]";break;
        case QtWarningMsg:out<<"[WRN]";break;
        case QtCriticalMsg:out<<"[CRT]";break;
        case QtFatalMsg:out<<"[FTL]";break;
        case QtInfoMsg:out<<"INF";break;
        default : out<<"[???]";break;
        }
        out <<" "<<msg<<"\n";
        file.close();
    }
}

int main(int argc, char *argv[])
{

    QApplication app(argc,argv);

    app.setWindowIcon(QIcon(":/icons/app.png"));

    //获取ela初始化
    ElaApplication::getInstance()->init();
    qInstallMessageHandler(logToFile);//安装日志钩子
    qDebug()<<"========程序运行成功========";
    app.setWindowIcon(QIcon(":/app/resources/app_icon.png"));
    //创建并显示窗口
    MainWindow w;
    w.showMaximized();
    qDebug()<<"窗口显示成功";

    return app.exec();
}
