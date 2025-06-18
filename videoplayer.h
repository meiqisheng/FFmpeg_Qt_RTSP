#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include <QThread>
#include <QImage>
#include <QAudioOutput>
#include <QMutex>

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

signals:
    void sig_GetOneFrame(QImage);
    void sig_GetRFrame(QImage);

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

private slots:
    void playAudioData(const QByteArray &audioData);
};
#endif // VIDEOPLAYER_H
