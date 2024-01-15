#include "audiocapturer.h"
#include "dlog.h"
#include "timesutil.h"

AudioCapturer::AudioCapturer(): CommonLooper()
{

}

AudioCapturer::~AudioCapturer()
{

}

RET_CODE AudioCapturer::Init(const Properties properties)
{
    audio_test = properties.GetProperty("audio_test",0);
    input_pcm_name_ =  properties.GetProperty("input_pcm_name","buweishui_48000_2_s16le.pcm");
    sample_rate_ =  properties.GetProperty("sample_rate",48000);
    nb_samples_ =  properties.GetProperty("nb_samples",1024);
    channels_ = properties.GetProperty("channels",2);

    pcm_buf_size_ = 2 * channels_ * nb_samples_;//PCM数据使用 s16 格式，即每个样本用 16 位（即 2 字节）表示。这就是乘以 2 的原因，因为每个样本需要 2 字节的存储空间。
    pcm_buf_ =new uint8_t[pcm_buf_size_];
    if(!pcm_buf_) {
        return RET_ERR_OUTOFMEMORY;
    }
    if(openPcmFile(input_pcm_name_.c_str()) < 0) {
        LogError("openPcmFile %s failed", input_pcm_name_.c_str());
        return RET_FAIL;
    }

    frame_duration_ = 1.0 * nb_samples_ / sample_rate_ * 1000;

    return RET_OK;
}

void AudioCapturer::Loop()
{
    pcm_total_duration_ = 0;
    pcm_start_time_ = TimesUtil::GetTimeMillisecond();
    while(true) {
        if ( request_abort_ ){
            break;
        }
        if(readPcmFile(pcm_buf_, pcm_buf_size_) == 0) {
            if(!is_first_time_) {
                is_first_time_ = true;
                LogInfo("is_first_time_");
            }
            if(callback_get_pcm_){
                callback_get_pcm_(pcm_buf_, pcm_buf_size_);
            }  
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    request_abort_ = false;
    closePcmFile();
}

void AudioCapturer::AddCallback(std::function<void (uint8_t *, int32_t)> callback)
{
    callback_get_pcm_ = callback;
}

int AudioCapturer::openPcmFile(const char *file_name)
{
    pcm_fp_ = fopen(file_name, "rb");
    if(!pcm_fp_)
    {
        return -1;
    }
    return 0;
}

int AudioCapturer::readPcmFile(uint8_t *pcm_buf, int32_t pcm_buf_size)
{
    // 时间检查: 确保在读取新帧之前已经过去了足够的时间。
    int64_t cur_time = TimesUtil::GetTimeMillisecond();
    int64_t dif = cur_time - pcm_start_time_;
    if(((int64_t)pcm_total_duration_) > dif) {
        return 1;          // 还没有到读取新一帧的时间
    }

    // 读取数据
    size_t ret = fread(pcm_buf_, 1, pcm_buf_size, pcm_fp_);
    if(ret != pcm_buf_size) {
        // 循环播放: 如果达到文件末尾，则从头开始
        ret = fseek(pcm_fp_, 0, SEEK_SET);
        ret = fread(pcm_buf_, 1, pcm_buf_size, pcm_fp_);
        if(ret != pcm_buf_size) {
            return -1;      // 出错
        }
    }

    // 更新持续时间: 增加 pcm_total_duration_ 以跟踪播放时间。
    pcm_total_duration_ += frame_duration_;

    return 0;
}

int AudioCapturer::closePcmFile()
{
    if(pcm_fp_)
        fclose(pcm_fp_);
    return 0;
}
