//
// Created by Administrator on 2017/10/3 0003.
//
#include <jni.h>
//导入android-log日志
#include <android/log.h>
//当前C++兼容C语言
extern "C"{
//avcodec:编解码(最重要的库)
#include "libavcodec/avcodec.h"
//avformat:封装格式处理
#include "libavformat/avformat.h"
//avutil:工具库(大部分库都需要这个库的支持)
#include "libavutil/imgutils.h"
#include "libavutil/time.h"
//swscale:视频像素数据格式转换
#include "libswscale/swscale.h"
//导入音频采样数据格式转换库
#include "libswresample/swresample.h"
}
#define LOG_TAG "FFmpeg"

#define LOGE(format, ...)  __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, format, ##__VA_ARGS__)
#define LOGI(format, ...)  __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, format, ##__VA_ARGS__)


AVFormatContext *ofmt_ctx = NULL;
AVStream *out_stream = NULL;
AVPacket pkt;
AVCodecContext *pCodecCtx = NULL;
AVCodec *pCodec = NULL;
AVFrame *yuv_frame;

int frame_count;
int src_width;
int src_height;
int y_length;
int uv_length;
int64_t start_time;

/**
 * 回调函数，用来把FFmpeg的log写到sdcard里面
 */
void live_log(void *ptr, int level, const char* fmt, va_list vl) {
    FILE *fp = fopen("/sdcard/123/live_log.txt", "a+");
    if(fp) {
        vfprintf(fp, fmt, vl);
        fflush(fp);
        fclose(fp);
    }
}
/**
 * 编码函数
 * avcodec_encode_video2被deprecated后，自己封装的
 */
int encode(AVCodecContext *pCodecCtx, AVPacket* pPkt, AVFrame *pFrame, int *got_packet) {
    int ret;

    *got_packet = 0;

    ret = avcodec_send_frame(pCodecCtx, pFrame);
    if(ret <0 && ret != AVERROR_EOF) {
        return ret;
    }

    ret = avcodec_receive_packet(pCodecCtx, pPkt);
    if(ret < 0 && ret != AVERROR(EAGAIN)) {
        return ret;
    }

    if(ret >= 0) {
        *got_packet = 1;
    }

    return 0;
}

JNIEXPORT void JNICALL
Java_com_gracefulengineer_samychen_MainActivity_clickTest(JNIEnv *env, jclass type) {

    //(char *)表示C语言字符串
    const char *configuration = avcodec_configuration();
    __android_log_print(ANDROID_LOG_INFO,"main","%s",configuration);
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_gracefulengineer_samychen_LiveActivity_streamerInit(JNIEnv *env, jobject instance,
                                                             jint width, jint height) {

    int ret = 0;
    const char *address = "rtmp://192.168.0.114/oflaDemo/test";

    src_width = width;
    src_height = height;
    //yuv数据格式里面的  y的大小（占用的空间）
    y_length = width * height;
    //u/v占用的空间大小
    uv_length = y_length / 4;

    //设置回调函数，写log
    av_log_set_callback(live_log);

    //激活所有的功能
    av_register_all();

    //推流就需要初始化网络协议
    avformat_network_init();

    //初始化AVFormatContext
    avformat_alloc_output_context2(&ofmt_ctx, NULL, "flv", address);
    if(!ofmt_ctx) {
        LOGE("Could not create output context\n");
        return -1;
    }

    //寻找编码器，这里用的就是x264的那个编码器了
    pCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if(!pCodec) {
        LOGE("Can not find encoder!\n");
        return -1;
    }

    //初始化编码器的context
    pCodecCtx = avcodec_alloc_context3(pCodec);
    pCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;  //指定编码格式
    pCodecCtx->width = width;
    pCodecCtx->height = height;
    pCodecCtx->time_base.num = 1;
    pCodecCtx->time_base.den = 30;
    pCodecCtx->bit_rate = 800000;
    pCodecCtx->gop_size = 300;

    if(ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
        pCodecCtx->flags |= CODEC_FLAG_GLOBAL_HEADER;
    }

    pCodecCtx->qmin = 10;
    pCodecCtx->qmax = 51;

    pCodecCtx->max_b_frames = 3;

    AVDictionary *dicParams = NULL;
    av_dict_set(&dicParams, "preset", "ultrafast", 0);
    av_dict_set(&dicParams, "tune", "zerolatency", 0);

    //打开编码器
    if(avcodec_open2(pCodecCtx, pCodec, &dicParams) < 0) {
        LOGE("Failed to open encoder!\n");
        return -1;
    }

    //新建输出流
    out_stream = avformat_new_stream(ofmt_ctx, pCodec);
    if(!out_stream) {
        LOGE("Failed allocation output stream\n");
        return -1;
    }
    out_stream->time_base.num = 1;
    out_stream->time_base.den = 30;
    //复制一份编码器的配置给输出流
    avcodec_parameters_from_context(out_stream->codecpar, pCodecCtx);

    //打开输出流
    ret = avio_open(&ofmt_ctx->pb, address, AVIO_FLAG_WRITE);
    if(ret < 0) {
        LOGE("Could not open output URL %s", address);
        return -1;
    }

    ret = avformat_write_header(ofmt_ctx, NULL);
    if(ret < 0) {
        LOGE("Error occurred when open output URL\n");
        return -1;
    }

    //初始化一个帧的数据结构，用于编码用
    //指定AV_PIX_FMT_YUV420P这种格式的
    yuv_frame = av_frame_alloc();
    uint8_t *out_buffer = (uint8_t *) av_malloc(av_image_get_buffer_size(pCodecCtx->pix_fmt, src_width, src_height, 1));
    av_image_fill_arrays(yuv_frame->data, yuv_frame->linesize, out_buffer, pCodecCtx->pix_fmt, src_width, src_height, 1);

    start_time = av_gettime();
    return 0;

}extern "C"
JNIEXPORT jint JNICALL
Java_com_gracefulengineer_samychen_LiveActivity_streamerHandle(JNIEnv *env, jobject instance,
                                                               jbyteArray data_) {
    jbyte *data = env->GetByteArrayElements(data_, NULL);
    int ret, i, resultCode;
    int got_packet = 0;
    resultCode = 0;

    /**
     * 这里就是之前说的NV21转为AV_PIX_FMT_YUV420P这种格式的操作了
     */
    memcpy(yuv_frame->data[0], data, y_length);
    for (i = 0; i < uv_length; i++) {
        *(yuv_frame->data[2] + i) = *(data + y_length + i * 2);
        *(yuv_frame->data[1] + i) = *(data + y_length + i * 2 + 1);
    }

    yuv_frame->format = pCodecCtx->pix_fmt;
    yuv_frame->width = src_width;
    yuv_frame->height = src_height;
    //yuv_frame->pts = frame_count;
    yuv_frame->pts = (1.0 / 30) * 90 * frame_count;


    pkt.data = NULL;
    pkt.size = 0;
    av_init_packet(&pkt);

    //进行编码
    ret = encode(pCodecCtx, &pkt, yuv_frame, &got_packet);
    if(ret < 0) {
        resultCode = -1;
        LOGE("Encode error\n");
        goto end;
    }
    if(got_packet) {
        LOGI("Encode frame: %d\tsize:%d\n", frame_count, pkt.size);
        frame_count++;
        pkt.stream_index = out_stream->index;

        //写PTS/DTS
        AVRational time_base1 = ofmt_ctx->streams[0]->time_base;
        AVRational r_frame_rate1 = {60, 2};
        AVRational time_base_q = {1, AV_TIME_BASE};
        int64_t calc_duration = (double)(AV_TIME_BASE) * (1 / av_q2d(r_frame_rate1));

        pkt.pts = av_rescale_q(frame_count * calc_duration, time_base_q, time_base1);
        pkt.dts = pkt.pts;
        pkt.duration = av_rescale_q(calc_duration, time_base_q, time_base1);
        pkt.pos = -1;

        //处理延迟
        int64_t pts_time = av_rescale_q(pkt.dts, time_base1, time_base_q);
        int64_t now_time = av_gettime() - start_time;
        if(pts_time > now_time) {
            av_usleep(pts_time - now_time);
        }

        ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
        if(ret < 0) {
            LOGE("Error muxing packet");
            resultCode = -1;
            goto end;
        }
        av_packet_unref(&pkt);
    }
    end:
    env->ReleaseByteArrayElements(data_, data, 0);
    return resultCode;
}extern "C"
JNIEXPORT jint JNICALL
Java_com_gracefulengineer_samychen_LiveActivity_streamerFlush(JNIEnv *env, jobject instance) {

    int ret;
    int got_packet;
    AVPacket packet;
    if(!(pCodec->capabilities & CODEC_CAP_DELAY)) {
        return 0;
    }

    while(1) {
        packet.data = NULL;
        packet.size = 0;
        av_init_packet(&packet);
        ret = encode(pCodecCtx, &packet, NULL, &got_packet);
        if(ret < 0) {
            break;
        }
        if(!got_packet) {
            ret = 0;
            break;
        }

        LOGI("Encode 1 frame size:%d\n", packet.size);

        AVRational time_base = ofmt_ctx->streams[0]->time_base;
        AVRational r_frame_rate1 = {60, 2};
        AVRational time_base_q = {1, AV_TIME_BASE};

        int64_t calc_duration = (double)(AV_TIME_BASE) * (1 / av_q2d(r_frame_rate1));

        packet.pts = av_rescale_q(frame_count * calc_duration, time_base_q, time_base);
        packet.dts = packet.pts;
        packet.duration = av_rescale_q(calc_duration, time_base_q, time_base);

        packet.pos = -1;
        frame_count++;
        ofmt_ctx->duration = packet.duration * frame_count;

        ret = av_interleaved_write_frame(ofmt_ctx, &packet);
        if(ret < 0) {
            break;
        }
    }

    //写文件尾
    av_write_trailer(ofmt_ctx);
    return 0;

}extern "C"
JNIEXPORT jint JNICALL
Java_com_gracefulengineer_samychen_LiveActivity_streamerRelease(JNIEnv *env, jobject instance) {

    if(pCodecCtx) {
        avcodec_close(pCodecCtx);
        pCodecCtx = NULL;
    }

    if(ofmt_ctx) {
        avio_close(ofmt_ctx->pb);
    }
    if(ofmt_ctx) {
        avformat_free_context(ofmt_ctx);
        ofmt_ctx = NULL;
    }

    if(yuv_frame) {
        av_frame_free(&yuv_frame);
        yuv_frame = NULL;
    }
}