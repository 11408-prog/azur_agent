#ifndef TTSCLIENT_H
#define TTSCLIENT_H

#include <QObject>
#include <QString>

class QProcess;

// edge-tts 语音合成客户端：异步调用 python/azur_tools/tts_cli.py 生成 mp3。
//
// 只依赖 QProcess，不阻塞 UI 线程，也不需要 Qt Multimedia（播放逻辑在
// MainWindow 里，用 QMediaPlayer/QAudioOutput）。输出文件写入 AppDataLocation
// 下的 tts_cache 目录，合成前会清理旧文件防止无限增长。
class TtsClient : public QObject
{
    Q_OBJECT
public:
    explicit TtsClient(QObject *parent = nullptr);
    ~TtsClient() override;

    // 异步合成语音。text 为空会直接 emit failed。
    void synthesize(const QString &text, const QString &voice);

    // 供测试/部署覆盖默认路径（默认自动从 ToolExecutor 定位解释器与 tts_cli.py）
    void setInterpreterPath(const QString &path);
    void setCliPath(const QString &path);

signals:
    void synthesized(const QString &filePath);
    void failed(const QString &errorMessage);

private slots:
    void onFinished();
    void onErrorOccurred();
    void onTimeout();

private:
    QString ttsCacheDir() const;
    void cleanupOldFiles() const;
    // interpreter/cli 由调用方（synthesize）解析后传入，避免直接使用未设置的成员
    // 导致 QProcess::start 拿到空程序名而同步触发 FailedToStart 后继续写空指针。
    void startProcess(const QString &interpreter, const QString &cli,
                      const QString &text, const QString &voice, const QString &outputPath);
    void cleanupProc();

    QProcess *proc_ = nullptr;
    QString interpreter_;
    QString cli_;
    bool timeoutTriggered_ = false;
};

#endif // TTSCLIENT_H
