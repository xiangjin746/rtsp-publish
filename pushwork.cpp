#include <functional>
#include "pushwork.h"
#include "dlog.h"
#include "avpublishtime.h"

PushWork::PushWork()
{

}
PushWork::~PushWork()
{
    // 从源头开始释放资源
    // 先释放音频、视频捕获
    if(audio_capturer_) {
        delete audio_capturer_;
    }

    if(video_capturer_) {
        delete video_capturer_;
    }

    if(audio_encoder_) {
        delete audio_encoder_;
    }

    if(fltp_buf_) {
        av_free(fltp_buf_);
    }

    if(audio_frame_) {
        av_frame_free(&audio_frame_);
    }

    if(pcm_s16le_fp_) {
        fclose(pcm_s16le_fp_);
    }

    if(aac_fp_) {
        fclose(aac_fp_);
    }

    if(video_encoder_) {
        delete video_encoder_;
    }
    LogInfo("~PushWork()");
}

RET_CODE PushWork::Init(const Properties &properties)
{
    int ret = 0;

    /*================================AUDIO===============================================*/
    // 音频test模式
    audio_test_ = properties.GetProperty("audio_test",0);
    input_pcm_name_ = properties.GetProperty("input_pcm_name", "input_48k_2ch_s16.pcm");
    
    // 麦克风采样属性
    mic_sample_rate_ =  properties.GetProperty("mic_sample_rate", 48000);
    mic_sample_fmt_ =  properties.GetProperty("mic_sample_fmt", AV_SAMPLE_FMT_S16);
    mic_channels_=  properties.GetProperty("mic_channels", 2);

    // 音频编码参数
    audio_sample_rate_  =  properties.GetProperty("audio_sample_rate", 48000);
    audio_bitrate_      =  properties.GetProperty("audio_bitrate",  128*1024);
    audio_channels_     =  properties.GetProperty("audio_channels", 2);
    audio_ch_layout_    =  av_get_default_channel_layout(audio_channels_);

    // 初始化publish time
    AVPublishTime::GetInstance()->Rest();

    // 初始化AAC音频编码器，如果失败则记录错误并返回
    audio_encoder_ = new AACEncoder();
    if(!audio_encoder_){
        LogError("new AACEncoder() failed");
        return RET_FAIL;        
    }

    // 使用设置的属性初始化音频编码器，如果失败则记录错误并返回
    // 需要什么样的采样格式是从编码器读取出来的
    Properties aud_codec_properties;
    aud_codec_properties.SetProperty("audio_sample_rate",audio_sample_rate_);
    aud_codec_properties.SetProperty("audio_bitrate",audio_bitrate_);
    aud_codec_properties.SetProperty("audio_channels",audio_channels_);

    if(audio_encoder_->Init(aud_codec_properties) != RET_OK) {
        LogError("AACEncoder  Init failed");
    }

    // 音频重采样和帧配置
    int frame_bytes2 = 0;
    // 默认读取出来的数据是s16的，编码器需要的是fltp, 需要做重采样
    // 手动把s16转成fltp
    fltp_buf_size_ = av_samples_get_buffer_size(NULL,audio_encoder_->GetChannels(),
                                                    audio_encoder_->GetFrameSamples(),
                                                    (enum AVSampleFormat)audio_encoder_->GetFormat(),1);
    fltp_buf_ = (uint8_t *)av_malloc(fltp_buf_size_);
    if(!fltp_buf_) {
        LogError("fltp_buf_ av_malloc failed");
        return RET_ERR_OUTOFMEMORY;
    }

    // 分配和配置一个AVFrame，用于存储转换后的FLTP音频样本
    audio_frame_ = av_frame_alloc();
    audio_frame_->format = audio_encoder_->GetFormat();
    // audio_frame_->format = AV_SAMPLE_FMT_FLTP;
    audio_frame_->nb_samples = audio_encoder_->GetFrameSamples();
    audio_frame_->channels = audio_encoder_->GetChannels();
    audio_frame_->channel_layout = audio_encoder_->GetChannelLayout();

    // 检查计算的缓冲区大小是否与编码器需要的大小一致
    frame_bytes2 = audio_encoder_->GetFrameBytes();
    if(fltp_buf_size_ != frame_bytes2) {
        LogError("frame_bytes1:%d != frame_bytes2:%d", fltp_buf_size_, frame_bytes2);
        return RET_FAIL;
    }

    // 为音频帧分配内存缓冲区，如果失败则记录错误并返回
    ret = av_frame_get_buffer(audio_frame_, 0);
    if(ret < 0) {
        LogError("audio_frame_ av_frame_get_buffer failed");
        return RET_FAIL;
    }


    // 设置音频捕获
    audio_capturer_ = new AudioCapturer();
    Properties aud_cap_properties;
    aud_cap_properties.SetProperty("audio_test",1);
    aud_cap_properties.SetProperty("input_pcm_name",input_pcm_name_);
    aud_cap_properties.SetProperty("sample_rate",48000);
    aud_cap_properties.SetProperty("nb_samples",1024);
    aud_cap_properties.SetProperty("channels",2);
    if(audio_capturer_->Init(aud_cap_properties) != RET_OK) {
        LogError("AudioCapturer Init failed");
        return RET_FAIL;
    }

    audio_capturer_->AddCallback(std::bind(&PushWork::PcmCallback,this,std::placeholders::_1,
                                                                        std::placeholders::_2));

    // if(audio_capturer_->Start() != RET_OK) {
    //     LogError("AudioCapturer Start failed");
    //     return RET_FAIL;
    // }

    /*================================VIDEO===============================================*/
    // 视频test模式
    video_test_ = properties.GetProperty("video_test",0);
    input_yuv_name_ = properties.GetProperty("input_yuv_name", "720x480_25fps_420p.yuv");
    
   // 桌面录制属性
    desktop_x_ = properties.GetProperty("desktop_x", 0);
    desktop_y_ = properties.GetProperty("desktop_y", 0);
    desktop_width_  = properties.GetProperty("desktop_width", 1920);
    desktop_height_ = properties.GetProperty("desktop_height", 1080);
    desktop_format_ = properties.GetProperty("desktop_pixel_format", AV_PIX_FMT_YUV420P);
    desktop_fps_ = properties.GetProperty("desktop_fps", 25);

    // 视频编码属性
    video_width_  = properties.GetProperty("video_width", desktop_width_);     // 宽
    video_height_ = properties.GetProperty("video_height", desktop_height_);   // 高
    video_fps_ = properties.GetProperty("video_fps", desktop_fps_);             // 帧率
    video_gop_ = properties.GetProperty("video_gop", video_fps_);
    video_bitrate_ = properties.GetProperty("video_bitrate", 1024*1024);   // 先默认1M fixedme
    video_b_frames_ = properties.GetProperty("video_b_frames", 0);   // b帧数量

    // 初始化视频编码器
    video_encoder_ = new H264Encoder();
    Properties  vid_codec_properties;
    vid_codec_properties.SetProperty("width", video_width_);
    vid_codec_properties.SetProperty("height", video_height_);
    vid_codec_properties.SetProperty("fps", video_fps_);            // 帧率
    vid_codec_properties.SetProperty("b_frames", video_b_frames_);
    vid_codec_properties.SetProperty("bitrate", video_bitrate_);    // 码率
    vid_codec_properties.SetProperty("gop", video_gop_);            // gop
    if(video_encoder_->Init(vid_codec_properties) != RET_OK)
    {
        LogError("H264Encoder Init failed");
        return RET_FAIL;
    }

    // 设置视频捕获
    video_capturer_ = new VideoCapturer();
    Properties vid_cap_properties;
    vid_cap_properties.SetProperty("video_test",1);
    vid_cap_properties.SetProperty("input_yuv_name",input_yuv_name_);
    vid_cap_properties.SetProperty("x",desktop_x_);
    vid_cap_properties.SetProperty("y",desktop_y_);
    vid_cap_properties.SetProperty("width",desktop_width_);
    vid_cap_properties.SetProperty("height",desktop_height_);
    vid_cap_properties.SetProperty("pixel_format",desktop_format_);
    vid_cap_properties.SetProperty("fps",desktop_fps_);
    if(video_capturer_->Init(vid_cap_properties) != RET_OK) {
        LogError("VideoCapturer Init failed");
        return RET_FAIL;
    }

    video_capturer_->AddCallback(std::bind(&PushWork::YuvCallback,this,std::placeholders::_1,
                                                                        std::placeholders::_2));

    if(video_capturer_->Start() != RET_OK) {
        LogError("VideoCapturer Start failed");
        return RET_FAIL;
    }

    return RET_OK;
}

RET_CODE PushWork::DeInit()
{
    if(audio_capturer_) {
        audio_capturer_->Stop();
        delete audio_capturer_;
        audio_capturer_ = NULL;
    }

    if(video_capturer_) {
        video_capturer_->Stop();
        delete video_capturer_;
        video_capturer_ = NULL;
    }
}

// 将s16le（16位有符号小端格式）音频数据转换为fltp（浮点平面）格式
void s16le_convert_to_fltp(short *s16le, float *fltp, int nb_samples)
{
    float *fltp_l = fltp;
    float *fltp_r = fltp + nb_samples;

    for (int i = 0; i < nb_samples; i++) {
        fltp_l[i] = s16le[i * 2] / 32768.0;
        fltp_r[i] = s16le[i * 2 + 1] / 32768.0;
    }
}

void PushWork::PcmCallback(uint8_t *pcm, int32_t size)
{
    int ret = 0;
    // 1 写入PCM数据到文件
    if (!pcm_s16le_fp_) {
        pcm_s16le_fp_ = fopen("push_dump_s16le.pcm", "wb");
    }

    if (pcm_s16le_fp_) {
        fwrite(pcm, 1, size, pcm_s16le_fp_);
        fflush(pcm_s16le_fp_);
    }

    // 2 转换PCM格式
    s16le_convert_to_fltp((short *)pcm, (float *)fltp_buf_, audio_frame_->nb_samples);

    // 3 准备音频帧
    if(av_frame_make_writable(audio_frame_) != 0) {
        LogError("av_frame_make_writable failed");
        return;
    }

    ret = av_samples_fill_arrays(audio_frame_->data,
                                audio_frame_->linesize,
                                fltp_buf_,
                                audio_frame_->channels,
                                audio_frame_->nb_samples,
                                (enum AVSampleFormat)audio_frame_->format,
                                0);
    if(ret < 0) {
        LogError("av_samples_fill_arrays failed");
        return;
    }

    // 4 音频编码
    int64_t pts = (int64_t)AVPublishTime::GetInstance()->get_audio_pts();
    int ptk_frame = 0;
    RET_CODE encode_ret = RET_OK;
    AVPacket *packet = audio_encoder_->Encode(audio_frame_, pts, 0, &ptk_frame, &encode_ret);
    if(encode_ret == RET_OK && packet) {
        // 5 写入AAC数据到文件
        // 5.1初始化文件指针，用于写入AAC数据
        if(!aac_fp_) {
            aac_fp_ = fopen("push_dump.aac", "wb");
            if(!aac_fp_) {
                LogError("fopen push_dump.aac failed");
                return;
            }
        }

        if(aac_fp_) {
            uint8_t adts_header[7];
            if (audio_encoder_->GetAdtsHeader(adts_header,packet->size) != RET_OK) {
                LogError("GetAdtsHeader failed");
                return;
            }
            fwrite(adts_header, 1, sizeof(adts_header), aac_fp_);
            fwrite(packet->data, 1, packet->size, aac_fp_);
            fflush(aac_fp_);
        }
    }

    // 6 处理结束与日志记录
    LogInfo("PcmCallback pts:%ld", pts);
    if(packet) {
        LogInfo("PcmCallback packet->pts:%ld", packet->pts);
        av_packet_free(&packet);
    } else {
        LogInfo("packet is null");
    }

}

void PushWork::YuvCallback(uint8_t *yuv, int32_t size)
{
    
    // 步骤1.1: 获取当前音频的演示时间戳（PTS）
    int64_t pts = (int64_t)AVPublishTime::GetInstance()->get_audio_pts();
    // 步骤2.1: 初始化编码过程中需要的变量
    int pkt_frame = 0;
    RET_CODE encode_ret = RET_OK;
    // 步骤2.2: 将YUV格式的数据编码为视频编码包
    AVPacket *packet =video_encoder_->Encode(yuv,size,pts,&pkt_frame,&encode_ret);    
    if(packet){     // 步骤3.1: 检查编码后的数据包是否存在
        // 步骤3.2: 如果文件指针为空，打开文件并写入SPS和PPS数据
        if(!h264_fp_) {
            h264_fp_ = fopen("push_dump.h264", "wb");
            if(!h264_fp_) {
                LogError("fopen push_dump.aac failed");
                return;
            }
        }
        // 步骤3.3: 写入SPS和PPS数据到文件
        uint8_t start_code[4] = {0, 0, 0, 1};
        fwrite(start_code, 1, 4, h264_fp_);
        fwrite(video_encoder_->get_sps_data(), 1, video_encoder_->get_sps_size(), h264_fp_);
        fwrite(start_code, 1, 4, h264_fp_);
        fwrite(video_encoder_->get_pps_data(), 1, video_encoder_->get_pps_size(), h264_fp_);

        // 步骤3.4: 将编码后的视频数据写入文件
        fwrite(packet->data, 1, packet->size, h264_fp_);
        fflush(h264_fp_);
    }

    // 步骤3.5: 记录日志并释放资源
    LogInfo("size:%d", size);
    if(packet) {
        LogInfo("YuvCallback packet->pts:%ld", packet->pts);
        av_packet_free(&packet);
    }else {
        LogInfo("packet is null");
    }
}
