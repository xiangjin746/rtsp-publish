#include "h264encoder.h"
#include "dlog.h"

H264Encoder::H264Encoder()
{

}

H264Encoder::~H264Encoder()
{
    if(ctx_) {
        avcodec_free_context(&ctx_);
    }
    if(frame_) {
        av_frame_free(&frame_);
    }
}

RET_CODE H264Encoder::Init(const Properties &properties)
{
    RET_CODE ret = RET_OK;

    // 1.读取视频属性
    width_      = properties.GetProperty("width", 1920);
    if(width_ ==0 || width_%2 != 0) {
        LogError("width:%d", width_);
        return RET_ERR_NOT_SUPPORT;
    }
    height_     = properties.GetProperty("height", 1080);
    if(height_ ==0 || height_%2 != 0) {
        LogError("height:%d", height_);
        return RET_ERR_NOT_SUPPORT;
    }
    fps_        = properties.GetProperty("fps", 25);
    b_frames_   = properties.GetProperty("b_frames", 0);
    bitrate_    = properties.GetProperty("bitrate", 500 * 1024);
    gop_        = properties.GetProperty("gop", fps_);
    annexb_     = properties.GetProperty("annexb", false);
    threads_    = properties.GetProperty("threads", 1);
    pix_fmt_    = properties.GetProperty("pix_fmt", AV_PIX_FMT_YUV420P);
    

    // 2.读取并选择编码器，如果未指定则使用默认H264编码
    codec_name_ = properties.GetProperty("codec_name", "default");
    if(codec_name_ == "default") {
        codec_ = avcodec_find_encoder(AV_CODEC_ID_H264);
    }else {
        codec_ = avcodec_find_encoder_by_name(codec_name_.c_str());
    }
    if(!codec_) {
        LogError("can't find encoder");
        return RET_FAIL;
    }

    // 3.配置编码器上下文
    ctx_ = avcodec_alloc_context3(codec_);
    if(!ctx_) {
        LogError("ctx_ h264 avcodec_alloc_context3 failed");
        return RET_FAIL;
    }

    // 3.1设置编码器上下文的视频宽高、码率、GOP大小、帧率、时间基、像素格式、编码器类型、b帧数量
    ctx_->width = width_;
    ctx_->height = height_;
    ctx_->bit_rate = bitrate_;
    ctx_->gop_size = gop_;
    ctx_->framerate.num = fps_;
    ctx_->framerate.den = 1;
    ctx_->time_base.num = 1;
    ctx_->time_base.den = fps_;
    ctx_->pix_fmt = (enum AVPixelFormat)pix_fmt_;
    ctx_->codec_type = AVMEDIA_TYPE_VIDEO;
    ctx_->max_b_frames = b_frames_;

    av_dict_set(&dict_, "preset", "medium", 0);
    av_dict_set(&dict_, "tune", "zerolatency", 0);
    av_dict_set(&dict_, "profile", "high", 0);

    ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    // 初始化音视频编码器
    ret = (RET_CODE)avcodec_open2(ctx_, codec_, &dict_);
    if(ret < 0) {
        char buf[1024] = { 0 };
        av_strerror(ret, buf, sizeof(buf) - 1);
        LogError("avcodec_open2 failed:%s", buf);
        return RET_FAIL;
    }

    // 4.从extradata读取SPS和PPS
    if(ctx_->extradata) {// 检查extradata是否存在
        uint8_t *sps = ctx_->extradata + 4;// 定位SPS
        int sps_len = 0;
        uint8_t *pps = NULL;
        int pps_len = 0;
        uint8_t *data = ctx_->extradata + 4;

        for (int i = 0; i < ctx_->extradata_size - 4; ++i) {
            if (0 == data[i] && 0 == data[i + 1] && 0 == data[i + 2] && 1 == data[i + 3]) { // 遍历extradata寻找PPS:
                pps = &data[i+4];
                break;
            }
        }
        sps_len = int(pps - sps) - 4;
        pps_len = ctx_->extradata_size - 4*2 - sps_len; // 计算SPS和PPS的长度:
        // 提取SPS和PPS:
        sps_.append(sps, sps + sps_len);
        pps_.append(pps, pps + pps_len);

    }

    // 5.分配视频帧内存，并设置帧的宽高和像素格式
    frame_ = av_frame_alloc();
    frame_->width = ctx_->width;
    frame_->height = ctx_->height;
    frame_->format = ctx_->pix_fmt;
    ret = (RET_CODE)av_frame_get_buffer(frame_,0);

    return ret;
}

AVPacket *H264Encoder::Encode(uint8_t *yuv,int size, const int64_t pts, int *pkt_frame, RET_CODE *ret)
{
    int local_ret = 0;
    *ret = RET_OK;
    *pkt_frame = 0;
    
    // 1. 上下文检查
    if(!ctx_){
        *ret = RET_FAIL;
        LogError("H264: no context");
        return NULL;
    }

    // 2. 设置帧的时间戳并发送帧
    if(yuv){
        int need_size = av_image_fill_arrays(frame_->data, frame_->linesize, yuv, 
                                            (AVPixelFormat)frame_->format,
                                            frame_->width, frame_->height, 1);
        if(need_size != size)  {
            LogError("need_size:%d != size:%d", need_size, size);
            *ret = RET_FAIL;
            return NULL;
        }
        frame_->pts = pts;
        frame_->pict_type = AV_PICTURE_TYPE_NONE;
        local_ret = avcodec_send_frame(ctx_,frame_);
        if(local_ret < 0){
            *pkt_frame = 1;
            if(local_ret == AVERROR(EAGAIN)){
                *ret = RET_ERR_EAGAIN;
                return NULL;
            }else if(local_ret == RET_ERR_EOF){
                *ret = RET_ERR_EOF;
                return NULL;
            }else{
                *ret = RET_FAIL;
                return NULL;
            }
        }
    }


    // 4. 接收编码后的数据包
    AVPacket *packet = av_packet_alloc();
    local_ret = avcodec_receive_packet(ctx_,packet);
    if(local_ret < 0){
        av_packet_free(&packet);
        *pkt_frame = 0;
        if(local_ret == AVERROR(EAGAIN)){
            *ret = RET_ERR_EAGAIN;
            return NULL;
        }else if(local_ret == RET_ERR_EOF){
            *ret = RET_ERR_EOF;
            return NULL;
        }else{
            *ret = RET_FAIL;
            return NULL;
        }
    }else{
        *ret = RET_OK;
        return packet;
    }
}
