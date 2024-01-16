#ifndef VIDEOCAPTURER_H
#define VIDEOCAPTURER_H

#include <functional>
#include "commonlooper.h"
#include "mediabase.h"

class VideoCapturer:public CommonLooper
{
public:
    VideoCapturer();
    virtual ~VideoCapturer();
    /**
     * @brief Init
     * @param "x", x起始位置，缺省为0
     *          "y", y起始位置，缺省为0
     *          "width", 宽度，缺省为屏幕宽带
     *          "height", 高度，缺省为屏幕高度
     *          "format", 像素格式，AVPixelFormat对应的值，缺省为AV_PIX_FMT_YUV420P
     *          "fps", 帧数，缺省为25
     * @return
     */
    RET_CODE Init(const Properties properties);

    virtual void Loop();
    void AddCallback(std::function<void(uint8_t*, int32_t)> callback);
private:
    int openYuvFile(const char *file_name);
    int readYuvFile(uint8_t *yuv_buf, int32_t yuv_buf_size);
    int closeYuvFile();

    int video_test_ = 0;
    std::string input_yuv_name_;
    int x_;
    int y_;
    int width_ = 0;
    int height_ = 0;
    int pixel_format_ = 0;
    int fps_;
    double frame_duration_ = 40;

    std::function<void(uint8_t *, int32_t)> callback_get_yuv_;
    uint8_t *yuv_buf_ = NULL; 
    int32_t yuv_buf_size_ = 0;
    FILE *yuv_fp_ = NULL;
    int64_t yuv_start_time_ = 0;
    double yuv_total_duration_ = 0;

    bool is_first_frame_ = false;
};

#endif // VIDEOCAPTURER_H