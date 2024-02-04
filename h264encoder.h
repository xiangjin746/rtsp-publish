#ifndef H264ENCODER_H
#define H264ENCODER_H

#include "mediabase.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>//图像处理相关的功能
}

class H264Encoder
{
public:
    H264Encoder();
    ~H264Encoder();

    RET_CODE Init(const Properties &properties);

    virtual AVPacket *Encode(uint8_t *yuv,int size, const int64_t pts, int *pkt_frame, RET_CODE *ret);

    inline uint8_t *get_sps_data() {
        return (uint8_t *)sps_.c_str();
    }
    inline int get_sps_size(){
        return sps_.size();
    }
    inline uint8_t *get_pps_data() {
        return (uint8_t *)pps_.c_str();
    }
    inline int get_pps_size(){
        return pps_.size();
    }
    inline int GetFps() {
        return fps_;
    }
    inline AVCodecContext *get_codec_context() { 
        return ctx_;
    }
private:
    int width_;
    int height_;
    int fps_;
    int b_frames_;
    int bitrate_;
    int gop_;
    bool annexb_  = false;
    int threads_;
    int pix_fmt_;

    std::string sps_;
    std::string pps_;
    std::string codec_name_;

    AVCodec *codec_ = NULL;
    AVCodecContext *ctx_ = NULL;
    AVDictionary *dict_ = NULL; //用于传递各种参数，例如编解码器的选项、过滤器的参数、容器格式的选项等等
    AVFrame *frame_ = NULL;
};

#endif // H264ENCODER_H
