#include "videoplayer.h"
#include <QAudioFormat>
#include <QDebug>

VideoPlayer::VideoPlayer(QObject *parent)
    : QThread(parent), mStopRequested(false),
      mAudioOutput(nullptr), mAudioIO(nullptr),
      mSwrCtx(nullptr), mDstSampleFmt(AV_SAMPLE_FMT_S16)
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

    // 打开RTSP流
    AVDictionary *options = nullptr;
    av_dict_set(&options, "rtsp_transport", "tcp", 0);
    av_dict_set(&options, "max_delay", "100", 0);

    const char *url = "rtsp://localhost:8554/mystream";
    if (avformat_open_input(&pFormatCtx, url, nullptr, &options) < 0) {
        qDebug() << "Could not open input stream";
        return;
    }

    if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
        qDebug() << "Could not find stream information";
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
