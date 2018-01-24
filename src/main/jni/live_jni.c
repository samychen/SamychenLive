//
// Created by Administrator on 2017/10/3 0003.
//
#include <jni.h>
//导入android-log日志
#include <android/log.h>
#include <stdio.h>
#include <libavfilter/avfilter.h>
#include <libavutil/opt.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
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
 * 定义filter相关的变量
 */
const char *filter_descr = "transpose=clock";  //顺时针旋转90度的filter描述
AVFilterContext *buffersink_ctx;
AVFilterContext *buffersrc_ctx;
AVFilterGraph *filter_graph;
int filterInitResult;
AVFrame *new_frame;

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

/**
 * 初始化filter
 */
int init_filters(const char *filters_descr) {
    /**
     * 注册所有AVFilter
     */
    avfilter_register_all();
    char args[512];
    int ret = 0;
    //参考雷神最简单的基于FFmpeg的AVFilter例子 http://blog.csdn.net/leixiaohua1020/article/details/29368911
    AVFilter *buffersrc  = avfilter_get_by_name("buffer");
    AVFilter *buffersink = avfilter_get_by_name("buffersink");
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE };

    //为FilterGraph分配内存
    filter_graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !filter_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    /**
     * 要填入正确的参数
     */
    snprintf(args, sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             src_width, src_height, pCodecCtx->pix_fmt,
             pCodecCtx->time_base.num, pCodecCtx->time_base.den,
             pCodecCtx->sample_aspect_ratio.num, pCodecCtx->sample_aspect_ratio.den);
    //创建并向FilterGraph中添加一个Filter
    ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in", args, NULL, filter_graph);
    if (ret < 0) {
        LOGE("Cannot create buffer source\n");
        goto end;
    }
    ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out", NULL, NULL, filter_graph);
    if (ret < 0) {
        LOGE("Cannot create buffer sink\n");
        goto end;
    }
    ret = av_opt_set_int_list(buffersink_ctx, "pix_fmts", pix_fmts, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        LOGE("Cannot set output pixel format\n");
        goto end;
    }

    outputs->name       = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    inputs->name       = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;
    //将一串通过字符串描述的Graph添加到FilterGraph中
    if ((ret = avfilter_graph_parse_ptr(filter_graph, filters_descr, &inputs, &outputs, NULL)) < 0) {
        LOGE("parse ptr error\n");
        goto end;
    }
    //检查FilterGraph的配置
    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0) {
        LOGE("parse config error\n");
        goto end;
    }

    new_frame = av_frame_alloc();
    //uint8_t *out_buffer = (uint8_t *) av_malloc(av_image_get_buffer_size(pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, 1));
    //av_image_fill_arrays(new_frame->data, new_frame->linesize, out_buffer, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, 1);

    end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}

JNIEXPORT void JNICALL
Java_com_gracefulengineer_samychen_MainActivity_clickTest(JNIEnv *env, jclass type) {

    //(char *)表示C语言字符串
    const char *configuration = avcodec_configuration();
    __android_log_print(ANDROID_LOG_INFO,"main","%s",configuration);
}

JNIEXPORT jint JNICALL
Java_com_gracefulengineer_samychen_LiveActivity_streamerInit(JNIEnv *env, jobject instance,
                                                             jint width, jint height) {
    int ret = 0;
    const char *address = "rtmp://192.168.31.101/oflaDemo/test";
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
    pCodecCtx->width = height;
    pCodecCtx->height = width;
    //编码配置参数说明参考 http://blog.csdn.net/chance_yin/article/details/16335625
    //帧率的基本单位，我们用分数来表示，用分数来表示的原因是，有很多视频的帧率是带小数的eg：NTSC 使用的帧率是29.97
    pCodecCtx->time_base.num = 1;//分子
    pCodecCtx->time_base.den = 30;//分母，设置帧率,要避免动作不流畅的最低是30,对下文设置pts有影响http://blog.csdn.net/dancing_night/article/details/45972361
    pCodecCtx->bit_rate = 800000;//设置采样参数，即比特率,采样码率越大，视频大小越大
    pCodecCtx->gop_size = 300;//每300帧插入1个I帧，I帧越少，视频越小
    //一些格式需要视频流数据头分开
    if(ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
        pCodecCtx->flags |= CODEC_FLAG_GLOBAL_HEADER;
    }
    //最大和最小量化系数  量化系数越小 视频越清晰http://cdmd.cnki.com.cn/Article/CDMD-10013-1015527877.htm
    pCodecCtx->qmin = 10;
    pCodecCtx->qmax = 51;
    //两个非B帧之间允许出现多少个B帧数,设置0表示不使用B帧,b 帧越多，图片越小。vcodec_encode_video2函数输出的延时仅仅跟max_b_frames的设置有关，
    //想进行实时编码，将max_b_frames设置为0便没有编码延时了
    pCodecCtx->max_b_frames = 3;
    // 编码延迟问题,最新使用FFmpeg进行H264的编码时，发现视频编码有延迟，不是实时编码，需要在调用avcodec_open2函数打开编码器时，设置AVDictionary参数
    AVDictionary *dicParams = NULL;
    //H264, 设置为编码延迟为立即编码
    if(pCodecCtx->codec_id == AV_CODEC_ID_H264)
    {
        av_dict_set(&dicParams, "preset", "superfast",   0);
        av_dict_set(&dicParams, "tune",   "zerolatency", 0);//zerolatency参数的作用是提搞编码的实时性
    }
    //H.265
    if(pCodecCtx->codec_id == AV_CODEC_ID_H265)
    {
        av_dict_set(&dicParams, "x265-params", "qp=20", 0);
        av_dict_set(&dicParams, "preset", "ultrafast", 0);
        av_dict_set(&dicParams, "tune", "zero-latency", 0);//zerolatency参数的作用是提搞编码的实时性
    }
    //Dump Information 输出格式信息
//    av_dump_format(ofmt_ctx, 0, out_file, 1);
//    av_dict_set(&dicParams, "preset", "superfast", 0);
//    av_dict_set(&dicParams, "tune", "zerolatency", 0);
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
    //设置输出流帧率和编码上下文一样
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
//    int result = avformat_open_input(ofmt_ctx, address,NULL, NULL);
//    if (result != 0)
//    {
//        char szError[256];
//        av_strerror(result, szError, 256);
//        LOGE("%s%d",szError,result);
//        LOGE("Call avformat_open_input function failed!\n");
//    }
    //写输出流头部
    ret = avformat_write_header(ofmt_ctx, NULL);
    if(ret < 0) {
        LOGE("Error occurred when open output URL\n");
        return -1;
    }
    //初始化一个帧的数据结构，用于编码用
    //指定AV_PIX_FMT_YUV420P这种格式的
    yuv_frame = av_frame_alloc();
    uint8_t *out_buffer = (uint8_t *) av_malloc(av_image_get_buffer_size(pCodecCtx->pix_fmt, src_width, src_height, 1));
    //将yuv_frame与out_buffer绑定
    av_image_fill_arrays(yuv_frame->data, yuv_frame->linesize, out_buffer, pCodecCtx->pix_fmt, src_width, src_height, 1);
    //保存当前时间
    start_time = av_gettime();
    /**
     * 初始化filter
     */
    filterInitResult = init_filters(filter_descr);
    if(filterInitResult < 0) {
        LOGE("Filter init error");
    }
    return 0;
}

JNIEXPORT jint JNICALL
Java_com_gracefulengineer_samychen_LiveActivity_streamerFlush(JNIEnv *env, jobject instance) {
    int ret;
    int got_packet;
    AVPacket packet;
    //如果是帧并行解码的话需要在codec初始化的时候设置codec->capabilities |= CODEC_CAP_DELAY
    //如果具备CODEC_CAP_DELAY.那么应该(或者是必须)在解码完所有帧后,继续循环调用解码接口,同时把输入包
    //AVPacket的数据指针data置为NULL,大小size置为0.直到返回got_pictrue为0为止.这样才能保证得到与输入帧对应数量的所有的图像.
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
        //timebase参考：http://blog.csdn.net/dancing_night/article/details/52101313 http://blog.csdn.net/vansbelove/article/details/53036602 http://blog.csdn.net/vansbelove/article/details/52996654
        //AVRarional time_base = {1,1,AV_TIME_BASE};
        //int64_t  timestamp = time/ time_base;  //内部时间戳
        //int64_t time = timestamp * time_base;//实际时间(秒)
        AVRational time_base = ofmt_ctx->streams[0]->time_base;
        AVRational r_frame_rate1 = {60, 2};
        AVRational time_base_q = {1, AV_TIME_BASE};//ffmpeg内部时间基的分数表示，实际上它是AV_TIME_BASE的倒数
        // http://blog.csdn.net/vansbelove/article/details/52997188
        //av_q2d把AVRatioal结构转换成double ,根据pts来计算一桢在整个视频中的时间位置：timestamp(秒) = pts * av_q2d(stream->time_base)
        //计算视频长度的方法：time(秒) = stream->duration * av_q2d(stream->time_base)
        //内部时间戳timestamp(ffmpeg内部时间戳) = AV_TIME_BASE * time(秒),time(秒) = AV_TIME_BASE_Q * timestamp(ffmpeg内部时间戳)
        int64_t calc_duration = (double)(AV_TIME_BASE) * (1 / av_q2d(r_frame_rate1));
		//同步音视频pts和dts 。 av_rescale_q不同时间基之间的转换函数
        //int64_t av_rescale_q(int64_t a, AVRational bq, AVRational cq)这个函数的作用是计算a * bq / cq，来把时间戳从一个时基调整到另外一个时基。
        //在进行时基转换的时候，我们应该首选这个函数，因为它可以避免溢出的情况发生。
        packet.pts = av_rescale_q(frame_count * calc_duration, time_base_q, time_base);
        packet.dts = packet.pts;
        packet.duration = av_rescale_q(calc_duration, time_base_q, time_base);
        //通过视频图像计数计数视频的时间
        packet.pos = -1;
        frame_count++;//放入视频的图像计数
        ofmt_ctx->duration = packet.duration * frame_count;
        //写入一个AVPacket到输出文件。这个方法在网络较好的情况下，一般是几个毫秒就返回发送成功。然后在网络较差的情况下，会一直卡在这里，没有返回
        //http://www.cnblogs.com/zzugyl/p/5417478.html
        ret = av_interleaved_write_frame(ofmt_ctx, &packet);
        if(ret < 0) {
            break;
        }
    }
    //写文件尾
    av_write_trailer(ofmt_ctx);
    return 0;
}
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
    if(filter_graph) {
        avfilter_graph_free(&filter_graph);
        filter_graph = NULL;
    }
    if(new_frame) {
        av_frame_free(&new_frame);
        new_frame = NULL;
    }
}

JNIEXPORT jint JNICALL
Java_com_gracefulengineer_samychen_LiveActivity_streamerHandle(JNIEnv *env, jobject instance,
                                                               jbyteArray data_, jlong timestamp) {
    jbyte *data = (*env)->GetByteArrayElements(env, data_, NULL);
    int ret, i, resultCode;
    int got_packet = 0;
    resultCode = 0;
    /**
     * 这里就是之前说的NV21转为AV_PIX_FMT_YUV420P这种格式的操作了，格式为：yyyyyyyyvuvu
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
    //yuv_frame->pts = (1.0 / 30) * 90 * frame_count;
    //pts =  1000000 * frame_count * frame_length / sample_rate;
    //(1,30)到(1,AV_TIME_BASE)的时间戳需要转换
    //FFmpeg的time_base实际上就是指时间的刻度，比如说当time_base为{1, 30}的时候，如果pts为20，
    //那么要变成time_base为{1, 1000000}刻度时的pts就要进行转换(20 * 1 / 30) / (1 / 1000000)
    //而且解码器那里有一个time_base，编码器又有自己的time_base，所以当进行操作后，需要进行一个time_base的转换才行
    yuv_frame->pts = timestamp * 30 / AV_TIME_BASE;//30是编码器的帧率
    pkt.data = NULL;
    pkt.size = 0;
    av_init_packet(&pkt);
    if (filterInitResult >= 0) {
        ret = 0;
        //向FilterGraph中加入一个AVFrame
        ret = av_buffersrc_add_frame(buffersrc_ctx, yuv_frame);
        if (ret >= 0) {
            //从FilterGraph中取出一个AVFrame
            ret = av_buffersink_get_frame(buffersink_ctx, new_frame);
            if (ret >= 0) {
                ret = encode(pCodecCtx, &pkt, new_frame, &got_packet);
            } else {
                LOGE("Error while getting the filtergraph\n");
            }
        } else {
            LOGE("Error while feeding the filtergraph\n");
        }
    }
    if(filterInitResult < 0 || ret < 0) {
        LOGE("encode from yuv data");
        /**
         * 因为通过filter后，packet的宽高已经改变了，初始化的编码器已经无法使用了，
         * 所以要兼容filter无法初始化的话，需要重新初始化一个对应宽高的编码器
         */
        //进行编码
        //ret = encode(pCodecCtx, &pkt, yuv_frame, &got_packet);
    }
    if(ret < 0) {
        resultCode = -1;
        LOGE("Encode error\n");
        goto end;
    }
    if(got_packet) {
        LOGI("Encode frame: %d\tsize:%d\n", frame_count, pkt.size);
        frame_count++;
        pkt.stream_index = out_stream->index;
        //将packet中的有效定时字段（timestamp/duration）从一个time_base转换为另一个time_base
        //FFmpeg的time_base实际上就是指时间的刻度,比如说当time_base为{1, 30}的时候，如果pts为20，
        //那么要变成time_base为{1, 1000000}刻度时的pts就要进行转换(20 * 1 / 30) / (1 / 1000000)
        //而且解码器那里有一个time_base，编码器又有自己的time_base，所以当进行操作后，需要进行一个time_base的转换才行
        //使用这个函数可以注释下面代码
        av_packet_rescale_ts(&pkt, pCodecCtx->time_base, out_stream->time_base);
        //写PTS/DTS  http://blog.csdn.net/vansbelove/article/details/53036602
        /*AVRational time_base1 = ofmt_ctx->streams[0]->time_base;
        AVRational r_frame_rate1 = {60, 2};
        AVRational time_base_q = {1, AV_TIME_BASE};
        int64_t calc_duration = (double)(AV_TIME_BASE) * (1 / av_q2d(r_frame_rate1));

        pkt.pts = av_rescale_q(frame_count * calc_duration, time_base_q, time_base1);
        pkt.dts = pkt.pts;
        pkt.duration = av_rescale_q(calc_duration, time_base_q, time_base1);
        pkt.pos = -1;

        //处理延迟.判断PTS和当前时间间隔是否相等,pts时间还原为内部时间戳
        int64_t pts_time = av_rescale_q(pkt.dts, time_base1, time_base_q);
        int64_t now_time = av_gettime() - start_time;
        if(pts_time > now_time) {
            av_usleep(pts_time - now_time);
        }*/
        //
        ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
        if(ret < 0) {
            LOGE("Error muxing packet");
            resultCode = -1;
            goto end;
        }
        av_packet_unref(&pkt);
    }

    end:
    (*env)->ReleaseByteArrayElements(env, data_, data, 0);
    return resultCode;
}