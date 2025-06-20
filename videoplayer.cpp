#include "videoplayer.h"
#include <QAudioFormat>
#include <QDebug>
#include <QFileInfo>
#include <QTimer>
#include <QCoreApplication>

VideoPlayer::VideoPlayer(QObject *parent)
    : QThread(parent), mStopRequested(false),
      mAudioOutput(nullptr), mAudioIO(nullptr),
      mSwrCtx(nullptr), mDstSampleFmt(AV_SAMPLE_FMT_S16),mIsPushing(false)
{
    avformat_network_init();
    av_register_all();
}

VideoPlayer::~VideoPlayer()
{
    stopPlay();
    avformat_network_deinit();
}

void VideoPlayer::startPlay()
{
    mStopRequested = false;
    start();
}

void VideoPlayer::stopPlay()
{
    QMutexLocker locker(&mStopMutex);
    mStopRequested = true;
    wait();
}

void VideoPlayer::initAudio()
{
    QAudioFormat format;
    format.setSampleRate(44100);
    format.setChannelCount(2);
    format.setSampleSize(16);
    format.setCodec("audio/pcm");
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setSampleType(QAudioFormat::SignedInt);

    mAudioOutput = new QAudioOutput(format, this);
    mAudioIO = mAudioOutput->start();
}

void VideoPlayer::cleanupAudio()
{
    if (mAudioOutput) {
        mAudioOutput->stop();
        delete mAudioOutput;
        mAudioOutput = nullptr;
    }
    if (mSwrCtx) {
        swr_free(&mSwrCtx);
        mSwrCtx = nullptr;
    }
}

void VideoPlayer::processAudioPacket(AVCodecContext *audioCodecCtx, AVPacket *packet)
{
    AVFrame *frame = av_frame_alloc();
    int gotFrame = 0;

    int ret = avcodec_decode_audio4(audioCodecCtx, frame, &gotFrame, packet);
    if (ret < 0) {
        av_frame_free(&frame);
        return;
    }

    if (gotFrame) {
        // 初始化重采样上下文
        if (!mSwrCtx) {
            mSwrCtx = swr_alloc_set_opts(nullptr,
                                        AV_CH_LAYOUT_STEREO,
                                        mDstSampleFmt,
                                        44100,
                                        audioCodecCtx->channel_layout,
                                        audioCodecCtx->sample_fmt,
                                        audioCodecCtx->sample_rate,
                                        0, nullptr);
            swr_init(mSwrCtx);
        }

        // 计算输出样本数
        int out_samples = av_rescale_rnd(
            swr_get_delay(mSwrCtx, audioCodecCtx->sample_rate) + frame->nb_samples,
            44100, audioCodecCtx->sample_rate, AV_ROUND_UP);

        // 分配输出缓冲区
        uint8_t *out_data[1];
        int out_linesize;
        av_samples_alloc(out_data, &out_linesize, 2, out_samples,
                        mDstSampleFmt, 0);

        // 执行重采样
        out_samples = swr_convert(mSwrCtx, out_data, out_samples,
                                (const uint8_t**)frame->data, frame->nb_samples);

        // 发送音频数据
        QByteArray audioData((const char*)out_data[0],
                           out_samples * 2 * av_get_bytes_per_sample(mDstSampleFmt));
        playAudioData(audioData);
        // 释放资源
        av_freep(&out_data[0]);
    }

    av_frame_free(&frame);
}

// videoplayer.cpp
void VideoPlayer::setStreamUrl(const QString &url) {
    QMutexLocker locker(&mStopMutex);
    mStreamUrl = url;
}

void VideoPlayer::run()
{
    AVFormatContext *pFormatCtx = nullptr;
    AVCodecContext *pVideoCodecCtx = nullptr, *pAudioCodecCtx = nullptr;
    AVCodec *pVideoCodec = nullptr, *pAudioCodec = nullptr;
    AVFrame *pFrame = nullptr, *pFrameRGB = nullptr;
    AVPacket packet;
    uint8_t *out_buffer = nullptr;
    SwsContext *img_convert_ctx = nullptr;

    int videoStream = -1, audioStream = -1;
    QByteArray urlData1 = m_transport.toUtf8();
    // 打开RTSP流
    AVDictionary *options = nullptr;
    av_dict_set(&options, "rtsp_transport", urlData1, 0);
    av_dict_set(&options, "max_delay", "100", 0);

    //const char *url = mStreamUrl.toUtf8().constData();
    QByteArray urlData = mStreamUrl.toUtf8();
    char *url = strdup(urlData.constData()); // 动态分配内存
    if (avformat_open_input(&pFormatCtx, url, nullptr, &options) < 0) {
        emit sig_StreamError(QString("Failed to open stream: %1").arg(mStreamUrl));
        return;
    }

    if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
        emit sig_StreamError("No valid video or audio stream found");
        avformat_close_input(&pFormatCtx);
        return;
    }

    // 查找视频和音频流
    for (unsigned int i = 0; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO && videoStream < 0) {
            videoStream = i;
        }
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO && audioStream < 0) {
            audioStream = i;
        }
    }

    // 初始化视频解码器
    if (videoStream >= 0) {
        pVideoCodecCtx = pFormatCtx->streams[videoStream]->codec;
        pVideoCodec = avcodec_find_decoder(pVideoCodecCtx->codec_id);
        if (!pVideoCodec || avcodec_open2(pVideoCodecCtx, pVideoCodec, nullptr) < 0) {
            qDebug() << "Could not open video codec";
            videoStream = -1;
        }
    }

    // 初始化音频解码器
    if (audioStream >= 0) {
        pAudioCodecCtx = pFormatCtx->streams[audioStream]->codec;
        pAudioCodec = avcodec_find_decoder(pAudioCodecCtx->codec_id);
        if (!pAudioCodec || avcodec_open2(pAudioCodecCtx, pAudioCodec, nullptr) < 0) {
            qDebug() << "Could not open audio codec";
            audioStream = -1;
        } else {
            initAudio();
        }
    }

    // 准备视频转换上下文
    if (videoStream >= 0) {
        pFrame = av_frame_alloc();
        pFrameRGB = av_frame_alloc();
        img_convert_ctx = sws_getContext(pVideoCodecCtx->width, pVideoCodecCtx->height,
                                        pVideoCodecCtx->pix_fmt, pVideoCodecCtx->width,
                                        pVideoCodecCtx->height, AV_PIX_FMT_RGB32,
                                        SWS_BICUBIC, nullptr, nullptr, nullptr);

        int numBytes = avpicture_get_size(AV_PIX_FMT_RGB32, pVideoCodecCtx->width, pVideoCodecCtx->height);
        out_buffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));
        avpicture_fill((AVPicture *)pFrameRGB, out_buffer, AV_PIX_FMT_RGB32,
                      pVideoCodecCtx->width, pVideoCodecCtx->height);
    }

    // 主播放循环
    while (!mStopRequested) {
        if (av_read_frame(pFormatCtx, &packet) < 0) {
            break;
        }

        if (packet.stream_index == videoStream && videoStream >= 0) {
            // 视频帧处理
            int got_picture = 0;
            avcodec_decode_video2(pVideoCodecCtx, pFrame, &got_picture, &packet);

            if (got_picture) {
                sws_scale(img_convert_ctx,
                         (uint8_t const * const *)pFrame->data,
                         pFrame->linesize, 0, pVideoCodecCtx->height,
                         pFrameRGB->data, pFrameRGB->linesize);

                QImage tmpImg((uchar *)out_buffer, pVideoCodecCtx->width,
                            pVideoCodecCtx->height, QImage::Format_RGB32);
                QImage image = tmpImg.copy();
                emit sig_GetOneFrame(image);

                // 提取红色通道
                for(int i = 0; i < pVideoCodecCtx->width; i++) {
                    for(int j = 0; j < pVideoCodecCtx->height; j++) {
                        QRgb rgb = image.pixel(i,j);
                        int r = qRed(rgb);
                        image.setPixel(i,j,qRgb(r,0,0));
                    }
                }
                emit sig_GetRFrame(image);
            }
        }
        else if (packet.stream_index == audioStream && audioStream >= 0) {
            // 音频帧处理
            processAudioPacket(pAudioCodecCtx, &packet);
        }

        av_packet_unref(&packet);
    }

    // 清理资源
    if (out_buffer) av_free(out_buffer);
    if (pFrameRGB) av_frame_free(&pFrameRGB);
    if (pFrame) av_frame_free(&pFrame);
    if (img_convert_ctx) sws_freeContext(img_convert_ctx);
    if (pVideoCodecCtx) avcodec_close(pVideoCodecCtx);
    if (pAudioCodecCtx) avcodec_close(pAudioCodecCtx);
    cleanupAudio();
    if (pFormatCtx) avformat_close_input(&pFormatCtx);
}

// videoplayer.cpp
void VideoPlayer::startPushing(const QString &inputUrl, const QString &outputUrl) {
    // 确保在主线程执行
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());

    // 安全终止现有进程
    if (mFFmpegProcess) {
        disconnect(mFFmpegProcess, nullptr, this, nullptr); // 断开所有信号连接
        if (mFFmpegProcess->state() == QProcess::Running) {
            mFFmpegProcess->terminate();
            if (!mFFmpegProcess->waitForFinished(1000)) {
                mFFmpegProcess->kill();
                mFFmpegProcess->waitForFinished();
            }
        }
        mFFmpegProcess->deleteLater(); // 安全删除
        mFFmpegProcess = nullptr;
    }

    try {
        mFFmpegProcess = new QProcess(this);
        mIsPushing = true;

        // 设置进程环境（可选）
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("FFREPORT", "file=ffmpeg_log.txt:level=32"); // 生成详细日志
        mFFmpegProcess->setProcessEnvironment(env);

        // 合并标准输出和错误输出
        mFFmpegProcess->setProcessChannelMode(QProcess::MergedChannels);

        // 实时输出日志
        connect(mFFmpegProcess, &QProcess::readyReadStandardOutput, this, [this]() {
            QString output = QString::fromLocal8Bit(mFFmpegProcess->readAllStandardOutput());
            emit sig_PushStatus(output.trimmed());
        });

        // 进程结束处理（自动重启逻辑）
        connect(mFFmpegProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, inputUrl, outputUrl](int code, QProcess::ExitStatus status) {
                mIsPushing = false;
                QString exitMsg = QString("推流进程结束，代码: %1").arg(code);
                emit sig_PushStatus(exitMsg);

                // 正常退出不重启（0表示成功退出）
                if (code == 0) return;

                // 异常退出时自动重启
                emit sig_PushStatus("检测到异常中断，3秒后尝试重启...");
                QTimer::singleShot(3000, this, [this, inputUrl, outputUrl]() {
                    if (!mIsPushing) { // 防止重复启动
                        startPushing(inputUrl, outputUrl);
                    }
                });
            });

        // 构建FFmpeg参数
        QStringList args;
        bool isCameraInput = inputUrl.contains("Camera", Qt::CaseInsensitive) ||
                            inputUrl.contains("CAM", Qt::CaseInsensitive) ||
                            inputUrl.contains("USB", Qt::CaseInsensitive) ||
                            !QFileInfo(inputUrl).exists();

        if (isCameraInput) {
            // 摄像头设备参数
            args << "-f" << "dshow"
                 << "-thread_queue_size" << "512"  // 增加线程队列大小
                 << "-i" << "video=" + inputUrl
                 << "-vcodec" << "libx264"
                 << "-preset:v" << "medium"
                 << "-tune:v" << "zerolatency"
                 << "-g" << "60"
                 << "-r" << "30"
                 << "-b:v" << "2M"
                 << "-bufsize" << "4M"  // 增加缓冲区
                 << "-maxrate" << "2M"
                 << "-rtsp_transport" << "tcp"
                 << "-reconnect" << "1"
                 << "-reconnect_at_eof" << "1"
                 << "-reconnect_streamed" << "1"
                 << "-reconnect_delay_max" << "5"
                 << "-timeout" << "5000000"
                 << "-muxdelay" << "0.5"
                 << "-loglevel" << "warning";  // 减少不必要的日志
        } else {
            // 文件输入参数
            args << "-re"
                 << "-stream_loop" << "-1"
                 << "-i" << inputUrl
                 << "-c" << "copy"
                 << "-rtsp_transport" << "tcp"
                 << "-fflags" << "+genpts"
                 << "-loglevel" << "warning";
        }
        args << "-f" << "rtsp" << outputUrl;

        emit sig_PushStatus(QString("启动推流命令: ffmpeg %1").arg(args.join(" ")));

        // 启动进程
        mFFmpegProcess->start("ffmpeg", args);

        // 检查是否成功启动
        if (!mFFmpegProcess->waitForStarted(3000)) {
            emit sig_PushStatus("启动FFmpeg失败");
            mFFmpegProcess->deleteLater();
            mFFmpegProcess = nullptr;
            mIsPushing = false;
        }

    } catch (const std::exception& e) {
        qCritical() << "Exception in startPushing:" << e.what();
        if (mFFmpegProcess) {
            mFFmpegProcess->deleteLater();
            mFFmpegProcess = nullptr;
        }
        mIsPushing = false;
        emit sig_PushStatus(QString("发生异常: %1").arg(e.what()));
    } catch (...) {
        qCritical() << "Unknown exception in startPushing";
        if (mFFmpegProcess) {
            mFFmpegProcess->deleteLater();
            mFFmpegProcess = nullptr;
        }
        mIsPushing = false;
        emit sig_PushStatus("发生未知异常");
    }
}

// 停止推流的函数
void VideoPlayer::stopPushing() {
    if (mFFmpegProcess && mFFmpegProcess->state() == QProcess::Running) {
        // 先尝试正常终止
        mFFmpegProcess->terminate();
        if (!mFFmpegProcess->waitForFinished(1000)) {
            mFFmpegProcess->kill();
        }
    }
    mIsPushing = false;
}

// videoplayer.cpp
void VideoPlayer::setTransportProtocol(const QString &protocol) {
    m_transport = protocol.toLower(); // 确保是小写
}
