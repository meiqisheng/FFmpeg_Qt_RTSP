#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include <QThread>
#include <QImage>
#include <QAudioOutput>
#include <QMutex>
#include <QProcess>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libavutil/avutil.h>
    #include <libavutil/imgutils.h>
    #include <libswscale/swscale.h>
    #include <libswresample/swresample.h>
}

class VideoPlayer : public QThread
{
    Q_OBJECT

public:
    explicit VideoPlayer(QObject *parent = nullptr);
    ~VideoPlayer();

    void startPlay();
    void stopPlay();
    void setStreamUrl(const QString &url);  // 新增方法
    void startPushing(const QString &inputUrl, const QString &outputUrl); // 新增推流方法
    void stopPushing(); // 停止推流

signals:
    void sig_GetOneFrame(QImage);
    void sig_GetRFrame(QImage);
    void sig_StreamError(const QString &errorMsg); // 新增错误信号
    void sig_PushStatus(const QString &message); // 推流状态信号
    void sig_RequireButtonReset();  // 需要复位按钮时触发

protected:
    void run() override;

private:
    QString mFileName;
    bool mStopRequested;
    QMutex mStopMutex;

    // 音频相关成员
    QAudioOutput *mAudioOutput;
    QIODevice *mAudioIO;
    SwrContext *mSwrCtx;
    AVSampleFormat mDstSampleFmt;

    void initAudio();
    void cleanupAudio();
    void processAudioPacket(AVCodecContext *audioCodecCtx, AVPacket *packet);
    //2025.6.19
    QString mStreamUrl;  // 存储流地址
    bool mIsPushing;
    QProcess *mFFmpegProcess = nullptr; // 用于调用FFmpeg命令行推流

private slots:
    void playAudioData(const QByteArray &audioData);
};
#endif // VIDEOPLAYER_H
