#include "rtsppusher.h"
#include "timesutil.h"
#include "dlog.h"

RtspPusher::RtspPusher( MessageQueue *msg_queue)
    :msg_queue_(msg_queue)
{
    LogInfo("RtspPusher create");
}

RtspPusher::~RtspPusher()
{
    DeInit();
}

static int decode_interrupt_cb(void *ctx) {
    // 设置超时时间，比如10秒
    RtspPusher *pusher = (RtspPusher *)ctx;

    // 检查当前时间是否已超过开始时间加上超时时间
    if (pusher->IsTimeout()) {
        // 如果超过了超时时间，返回1表示中断处理
        LogWarn("timeout:%dms", pusher->GetTimeout());
        return 1;
    }

    // 没有超时，返回0表示继续处理
    // LogInfo("block time:%lld", pusher->GetBlockTime());
    return 0;
}


RET_CODE RtspPusher::Init(const Properties &properties)
{
    // Step 1: 从属性集合中获取必要的参数值
    url_                    = properties.GetProperty("rtsp_url","");
    rtsp_transport_         = properties.GetProperty("rtsp_transport","");
    rtsp_timeout_           = properties.GetProperty("rtsp_timeout",5000);
    max_queue_duration_     = properties.GetProperty("rtsp_max_queue_duration",1000);
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

    fmt_ctx_->interrupt_callback.callback = decode_interrupt_cb;
    fmt_ctx_->interrupt_callback.opaque = this;
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
    RestTimeout();
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

    // LogInfo("sleep_for into");
    // std::this_thread::sleep_for(std::chrono::seconds(10));  //人为制造延迟
    // LogInfo("sleep_for leave");

    while (true) {
        if(request_abort_) {
            LogInfo("abort request");
            break;
        }

        debugQueue(debug_interval_);
        checkPacketQueueDuration();
        // std::this_thread::sleep_for(std::chrono::milliseconds(100));  //人为制造延迟

        ret = queue_->PopWithTimeout(&pkt, media_type, 1000);
        if(1 == ret) {
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

    RestTimeout();
    LogInfo("av_write_trailer ok");
}

// 按时间间隔打印packetqueue的状况
void RtspPusher::debugQueue(int64_t interval)
{
    // 获取当前系统的毫秒级时间，用于比较时间间隔
    int64_t cur_time = TimesUtil::GetTimeMillisecond();
    // 如果当前时间与上一次调试打印时间的差值大于指定的时间间隔，则执行打印逻辑
    if(cur_time - pre_debug_time_ > interval) { 
        // 获取音视频队列的状态，包括音视频的持续时间
        PacketQueueStats stats;
        queue_->GetStats(&stats);
        // 打印音视频队列持续时间的调试信息
        LogInfo("duration:a=%lldms, v=%lldms", stats.audio_duration, stats.video_duration);
        // 更新上一次调试打印的时间为当前时间，为下一次打印准备
        pre_debug_time_ = cur_time;
    }
}  

// 监测队列的缓存情况
void RtspPusher::checkPacketQueueDuration()
{
    // Step 1: 获取队列状态
    PacketQueueStats stats;
    queue_->GetStats(&stats);

    // Step 2: 判断队列持续时间是否超过最大值
    if(stats.audio_duration > max_queue_duration_ || stats.video_duration > max_queue_duration_) {
        // Step 3: 通知消息队列
        msg_queue_->notify_msg3(MSG_RTSP_QUEUE_DURATION, stats.audio_duration, stats.video_duration);
        // Step 4: 打印警告信息
        LogWarn("drop packet -> a:%lld, v:%lld, th:%d", stats.audio_duration, stats.video_duration, max_queue_duration_);
        // Step 5: 丢弃部分数据包
        queue_->Drop(false, max_queue_duration_);
    }
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
        msg_queue_->notify_msg2(MSG_RTSP_ERROR, ret);
        char str_error[512] = {0};
        av_strerror(ret, str_error, sizeof(str_error) -1);
        LogError("av_opt_set failed:%s", str_error);
        return -1;
    }
    
    RestTimeout();
    return 0;
}

//超时处理
bool RtspPusher::IsTimeout(){
    if(TimesUtil::GetTimeMillisecond() - pre_time_ > rtsp_timeout_) {
        return true;
    }
    return false;
}

//调用的地方就三个，1.初始化时，2.退出时，3.发一个包结束后
void RtspPusher::RestTimeout() {
    pre_time_ = TimesUtil::GetTimeMillisecond();
}

int RtspPusher::GetTimeout() {
    return rtsp_timeout_;
}

int64_t RtspPusher::GetBlockTime() {
    return TimesUtil::GetTimeMillisecond() - pre_time_;
}
