#include "aacencoder.h"
#include "dlog.h"


AACEncoder::AACEncoder()
{

}

AACEncoder::~AACEncoder()
{
    if(!ctx_){
        avcodec_free_context(&ctx_);
    }
}

RET_CODE AACEncoder::Init(const Properties &properties)
{
    // 1. 获取参数
    sample_rate_    = properties.GetProperty("sample_rate",48000);
    bitrate_        = properties.GetProperty("bitrate",128*1024);
    channels_       = properties.GetProperty("channels",2);
    channel_layout_ = properties.GetProperty("channel_layout",(int)av_get_default_channel_layout(channels_));

    // 2. 查找并设置编码器
    codec_ = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if(!codec_) return RET_ERR_MISMATCH_CODE;

    // 3. 分配编码器上下文
    ctx_ = avcodec_alloc_context3(codec_);
    if(!ctx_){
        LogError("AAC: avcodec_alloc_context3 failed");
        return RET_ERR_OUTOFMEMORY;
    }

    // 4. 设置编码器参数
    ctx_->channels              = channels_;
    ctx_->channel_layout        = channel_layout_;
    ctx_->sample_fmt            = AV_SAMPLE_FMT_FLTP;
    ctx_->sample_rate           = sample_rate_;
    ctx_->bit_rate              = bitrate_;
    ctx_->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;   //设置编码器或解码器遵守标准的严格程度的选项

    // 5. 打开编码器
    if(avcodec_open2(ctx_, codec_, NULL) < 0) {
        LogError("AAC: can't avcodec_open2");
        avcodec_free_context(&ctx_);
        return RET_FAIL;
    }
}

AVPacket *AACEncoder::Encode(AVFrame *frame, const int64_t pts, int flush, int *pkt_frame, RET_CODE *ret)
{
    int local_ret = 0;
    *pkt_frame = 0;
    
    // 1. 上下文检查
    if(!ctx_){
        *ret = RET_FAIL;
        LogError("AAC: no context");
        return NULL;
    }

    // 2. 设置帧的时间戳并发送帧
    if(frame){
        frame->pts = pts;
        local_ret = avcodec_send_frame(ctx_,frame);
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

    // 3. Flush 编码器
    if(flush){
        avcodec_flush_buffers(ctx_);
    }

    // 4. 接收编码后的数据包
    AVPacket *packet = av_packet_alloc();
    local_ret = avcodec_receive_packet(ctx_,packet);
    if(local_ret < 0){
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

RET_CODE AACEncoder::GetAdtsHeader(uint8_t *adts_header, int aac_length)
{
    uint8_t freqIdx = 0;    //0: 96000 Hz  3: 48000 Hz 4: 44100 Hz
    switch (ctx_->sample_rate)
    {
    case 96000: freqIdx = 0; break;
    case 88200: freqIdx = 1; break;
    case 64000: freqIdx = 2; break;
    case 48000: freqIdx = 3; break;
    case 44100: freqIdx = 4; break;
    case 32000: freqIdx = 5; break;
    case 24000: freqIdx = 6; break;
    case 22050: freqIdx = 7; break;
    case 16000: freqIdx = 8; break;
    case 12000: freqIdx = 9; break;
    case 11025: freqIdx = 10; break;
    case 8000: freqIdx = 11; break;
    case 7350: freqIdx = 12; break;
    default:
        LogError("can't support sample_rate:%d");
        freqIdx = 4;
        return RET_FAIL;
    }
    uint8_t ch_cfg = ctx_->channels;
    uint32_t frame_length = aac_length + 7;
    adts_header[0] = 0xFF;
    adts_header[1] = 0xF1;
    adts_header[2] = ((ctx_->profile) << 6) + (freqIdx << 2) + (ch_cfg >> 2);
    adts_header[3] = (((ch_cfg & 3) << 6) + (frame_length  >> 11));
    adts_header[4] = ((frame_length & 0x7FF) >> 3);
    adts_header[5] = (((frame_length & 7) << 5) + 0x1F);
    adts_header[6] = 0xFC;
    return RET_OK;
}
