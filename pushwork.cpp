#include <functional>
#include "pushwork.h"
#include "dlog.h"

PushWork::PushWork()
{

}

RET_CODE PushWork::Init(const Properties &properties)
{
    // 音频test模式
    audio_test_ = properties.GetProperty("audio_test",0);
    input_pcm_name_ = properties.GetProperty("input_pcm_name", "input_48k_2ch_s16.pcm");
    
    // 麦克风采样属性
    mic_sample_rate_ =  properties.GetProperty("mic_sample_rate", 48000);
    mic_sample_fmt_ =  properties.GetProperty("mic_sample_fmt", AV_SAMPLE_FMT_S16);
    mic_channels_=  properties.GetProperty("mic_channels", 2);

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

    if(audio_capturer_->Start() != RET_OK) {
        LogError("AudioCapturer Start failed");
        return RET_FAIL;
    }

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

    // 设置视频捕获
    video_capturer_ = new VideoCapturer();
    Properties vid_cap_properties;
    vid_cap_properties.SetProperty("video_test",1);
    vid_cap_properties.SetProperty("input_yuv_name_",input_yuv_name_);
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

void PushWork::PcmCallback(uint8_t *pcm, int32_t size)
{
    LogInfo("size:%d", size);
}

void PushWork::YuvCallback(uint8_t *yuv, int32_t size)
{
    LogInfo("size:%d", size);
}
