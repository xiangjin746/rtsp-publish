#include "rtsppusher.h"
#include "dlog.h"

RtspPusher::RtspPusher()
{
    LogInfo("RtspPusher create");
}

RtspPusher::~RtspPusher()
{
    DeInit();
}

RET_CODE RtspPusher::Init(const Properties &properties)
{
    // Step 1: 从属性集合中获取必要的参数值
    url_                    = properties.GetProperty("rtsp_url","");
    rtsp_transport_         = properties.GetProperty("rtsp_transport","");
    audio_frame_duration_   = properties.GetProperty("audio_frame_duration",0);
    video_frame_duration_   = properties.GetProperty("video_frame_duration",0);

    // Step 2: 检查必要的参数是否为空，如果为空则输出错误日志并返回错误码
    if(url_ == "") {
        LogError("url is null");
        return RET_FAIL;
    }
    if(rtsp_transport_ == "") {
        LogError("rtsp_transport is null, use udp or tcp");
        return RET_FAIL;
    }

    // Step 3: 初始化网络库（使用 FFmpeg 的 avformat_network_init 函数）
    int ret = 0;
    char str_error[512] = {0};
    ret = avformat_network_init();
    if(ret < 0) {
        av_strerror(ret,str_error,sizeof(str_error) - 1);
        LogError("avformat_network_init failed:%s", str_error);
        return RET_FAIL;
    }

    // Step 4: 分配 AVFormatContext 结构体（使用 FFmpeg 的 avformat_alloc_output_context2 函数）
    ret = avformat_alloc_output_context2(&fmt_ctx_, NULL, "rtsp", url_.c_str());
    if(ret < 0) {
        av_strerror(ret,str_error,sizeof(str_error) - 1);
        LogError("avformat_alloc_output_context2 failed:%s", str_error);
        return RET_FAIL;
    }

    // Step 5: 设置 AVFormatContext 的私有数据，指定 rtsp_transport 的值
    ret = av_opt_set(fmt_ctx_->priv_data,"rtsp_transport",rtsp_transport_.c_str(),0);
    if(ret < 0) {
        av_strerror(ret,str_error,sizeof(str_error) - 1);
        LogError("av_opt_set failed:%s", str_error);
        return RET_FAIL;
    }

    // Step 6: 创建 PacketQueue 对象（用于存储音视频帧的队列）
    queue_ = new PacketQueue(audio_frame_duration_,video_frame_duration_);
    if(!queue_) {
        LogError("new PacketQueue failed");
        return RET_ERR_OUTOFMEMORY;
    }
    return RET_OK;
}

void RtspPusher::DeInit()
{
    if(queue_) { 
        queue_->Abort();
    }
    Stop();
    if(fmt_ctx_) {
        avformat_free_context(fmt_ctx_);
        fmt_ctx_ = NULL;
    }
    if(queue_) { 
        delete queue_;
        queue_ = NULL;
    }
}

RET_CODE RtspPusher::Push(AVPacket *pkt, MediaType media_type)
{
    int ret = queue_->Push(pkt, media_type);
    if(ret < 0) {
        return RET_FAIL;
    } else {
        return RET_OK;
    }
}

RET_CODE RtspPusher::Connect()
{
    if(!audio_stream_ && !video_stream_) {
        return RET_FAIL;
    }
    // 连接服务器
    int ret = avformat_write_header(fmt_ctx_, NULL);
    if(ret < 0) {
        char str_error[512] = {0};
        av_strerror(ret, str_error, sizeof(str_error) -1);
        LogError("av_opt_set failed:%s", str_error);
        return RET_FAIL;
    }
    LogInfo("avformat_write_header ok");
    return Start();     // 启动线程
}

RET_CODE RtspPusher::ConfigVideoStream(const AVCodecContext *ctx)
{
    // Step 1: 检查fmt_ctx是否为null
    if(!fmt_ctx_) {
        LogError("fmt_ctx is null");
        return RET_FAIL;
    }

    // Step 2: 检查ctx是否为null
    if(!ctx) {
        LogError("ctx is null");
        return RET_FAIL;
    }

    // Step 3: 添加视频流
    AVStream *vs = avformat_new_stream(fmt_ctx_,NULL);
    if(!vs) {
        LogError("avformat_new_stream failed");
        return RET_FAIL;
    }
    // Step 4: 设置视频流的codec_tag为0
    vs->codecpar->codec_tag = 0;
    // Step 5: 从编码器拷贝信息到视频流
    avcodec_parameters_from_context(vs->codecpar,ctx);


    // Step 6: 设置video_ctx_, video_stream_, video_index_等成员变量
    video_ctx_ = (AVCodecContext *) ctx;
    video_stream_ = vs;
    video_index_  =  vs->index;       // 整个索引非常重要 fmt_ctx_根据index判别 音视频包
    // Step 7: 返回成功标志
    return RET_OK;
}

RET_CODE RtspPusher::ConfigAudioStream(const AVCodecContext *ctx)
{
    // Step 1: 检查fmt_ctx是否为null
    if(!fmt_ctx_) {
        LogError("fmt_ctx is null");
        return RET_FAIL;
    }

    // Step 2: 检查ctx是否为null
    if(!ctx) {
        LogError("ctx is null");
        return RET_FAIL;
    }

    // Step 3: 添加音频流
    AVStream *as = avformat_new_stream(fmt_ctx_,NULL);
    if(!as) {
        LogError("avformat_new_stream failed");
        return RET_FAIL;
    }
    // Step 4: 设置视频流的codec_tag为0
    as->codecpar->codec_tag = 0;
    // Step 5: 从编码器拷贝信息到视频流
    avcodec_parameters_from_context(as->codecpar,ctx);


    // Step 6: 设置video_ctx_, video_stream_, video_index_等成员变量
    audio_ctx_ = (AVCodecContext *) ctx;
    audio_stream_ = as;
    audio_index_  =  as->index;       // 整个索引非常重要 fmt_ctx_根据index判别 音视频包
    // Step 7: 返回成功标志
    return RET_OK;
}

void RtspPusher::Loop()
{
    LogInfo("Loop into");
    int ret = 0;
    AVPacket *pkt = NULL;
    MediaType media_type;

    while (true) {
        if(request_abort_) {
            LogInfo("abort request");
            break;
        }
        ret = queue_->PopWithTimeout(&pkt, media_type, 2000);
        if(0 == ret) {
            switch (media_type) {
                if(request_abort_) {
                    LogInfo("abort request");
                    av_packet_free(&pkt);
                    break;
                }
                case E_VIDEO_TYPE:
                    ret = sendPacket(pkt, media_type);
                    if(ret < 0) {
                        LogError("send video Packet failed");
                    }
                    av_packet_free(&pkt);
                    break;
                case E_AUDIO_TYPE:
                    ret = sendPacket(pkt, media_type);
                    if(ret < 0) {
                        LogError("send audio Packet failed");
                    }
                    av_packet_free(&pkt);
                    break;
                default:
                    break;
            }
        }
    }
    ret = av_write_trailer(fmt_ctx_);
    if(ret < 0) {
        char str_error[512] = {0};
        av_strerror(ret, str_error, sizeof(str_error) -1);
        LogError("av_write_trailer failed:%s", str_error);
        return;
    }
    LogInfo("avformat_write_header ok");
}

int RtspPusher::sendPacket(AVPacket *pkt, MediaType media_type)
{
    AVRational dst_time_base;
    AVRational src_time_base = {1, 1000};      // 我们采集、编码 时间戳单位都是ms

    if(E_VIDEO_TYPE == media_type) {
        pkt->stream_index = video_index_;
        dst_time_base = video_stream_->time_base;
    } else if(E_AUDIO_TYPE == media_type) {
        pkt->stream_index = audio_index_;
        dst_time_base = audio_stream_->time_base;
    } else {
        LogError("unknown mediatype:%d", media_type);
        return -1;
    }
    pkt->pts = av_rescale_q(pkt->pts, src_time_base, dst_time_base);
    pkt->duration = 0;

    int ret = av_write_frame(fmt_ctx_, pkt);
    if(ret < 0) {
        char str_error[512] = {0};
        av_strerror(ret, str_error, sizeof(str_error) -1);
        LogError("av_opt_set failed:%s", str_error);
        return -1;
    }

    return 0;
}
