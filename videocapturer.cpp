#include "videocapturer.h"
#include "dlog.h"
#include "timesutil.h"
#include "avpublishtime.h"

VideoCapturer::VideoCapturer()
{

}

VideoCapturer::~VideoCapturer()
{
    if(yuv_buf_){
        delete [] yuv_buf_;
    }
    if(yuv_fp_){
        fclose(yuv_fp_);
    }
}

RET_CODE VideoCapturer::Init(const Properties properties)
{
    video_test_ = properties.GetProperty("video_test",0);
    input_yuv_name_ =  properties.GetProperty("input_yuv_name_","720x480_25fps_420p.yuv");

    x_ =  properties.GetProperty("x",0);
    y_ =  properties.GetProperty("y",0);
    width_ =  properties.GetProperty("width",720);
    height_ = properties.GetProperty("height",480);
    pixel_format_ = properties.GetProperty("pixel_format", 0);  
    fps_  = properties.GetProperty("fps", 25);

    // yuv_buf_size_ = (width_ + width_ % 2) * (height_ + height_ % 2) * 1.5; // 一帧yuv占用的字节数量
    // yuv_buf_ = new uint8_t[yuv_buf_size_];
    // if(!yuv_buf_) {
    //     return RET_ERR_OUTOFMEMORY;
    // }
    if(openYuvFile(input_yuv_name_.c_str()) < 0) {
        LogError("openYuvFile %s failed", input_yuv_name_.c_str());
        return RET_FAIL;
    }

    frame_duration_ = 1000.0 / fps_;    // 单位是毫秒的

    return RET_OK;
}

void VideoCapturer::Loop()
{
    yuv_buf_size_ =(width_ + width_%2)  * (height_ + height_%2) * 1.5;   // 一帧yuv占用的字节数量
    yuv_buf_ = new uint8_t[yuv_buf_size_];
    yuv_total_duration_ = 0;
    yuv_start_time_ = TimesUtil::GetTimeMillisecond();
    LogInfo("into loop while");

    while (true)
    {
        if(request_abort_){
            break;
        }

        if(readYuvFile(yuv_buf_, yuv_buf_size_) == 0)
        {
            if(!is_first_frame_) {
                is_first_frame_ = true;
                LogInfo("video: %s:t%u", AVPublishTime::GetInstance()->getVInTag(),
                        AVPublishTime::GetInstance()->getCurrenTime());
            }
            if(callback_get_yuv_)
            {
                callback_get_yuv_(yuv_buf_, yuv_buf_size_);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    LogInfo("exit loop while");
    
}

void VideoCapturer::AddCallback(std::function<void (uint8_t *, int32_t)> callback)
{
    callback_get_yuv_ = callback;
}

int VideoCapturer::openYuvFile(const char *file_name)
{
    yuv_fp_ = fopen(file_name,"r");
    if(!yuv_fp_){
        return RET_FAIL;
    }
    return RET_OK;
}

int VideoCapturer::readYuvFile(uint8_t *yuv_buf, int32_t yuv_buf_size)
{
    int64_t cur_time = TimesUtil::GetTimeMillisecond();
    int64_t dif = cur_time - yuv_start_time_;
    // LogInfo("readYuvFile:%lld, %lld\n", yuv_total_duration_, dif);
    if((int64_t)yuv_total_duration_ > dif)
        return RET_ERR_EOF;
    // 该读取数据了
    size_t ret = fread(yuv_buf, 1, yuv_buf_size, yuv_fp_);
    if(ret != yuv_buf_size)
    {
        // 从文件头部开始读取
        ret = fseek(yuv_fp_, 0, SEEK_SET);
        ret = fread(yuv_buf, 1, yuv_buf_size, yuv_fp_);
        LogInfo("ret:%d,yuv_buf_size:%d", ret, yuv_buf_size);
        if(ret != yuv_buf_size)
        {

            return RET_FAIL;
        }
    }
    // LogInfo("yuv_total_duration_:%lldms, %lldms", (int64_t)yuv_total_duration_, dif);
    yuv_total_duration_ += frame_duration_;  //
    return RET_OK;
}

int VideoCapturer::closeYuvFile()
{
    if(yuv_fp_)
        fclose(yuv_fp_);
    return RET_OK;
}
