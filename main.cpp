#include <iostream>
#include "dlog.h"
#include "pushwork.h"
#include <thread>
using namespace std;

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/audio_fifo.h>
}

using namespace std;

#define RTSP_URL "rtsp://192.168.2.132/live/livestream"

int main()
{
    cout << "Hello World!" << endl;

    init_logger("rtsp_push.log", S_INFO);

    {
        PushWork push_work;
        Properties properties;

        // 音频test模式
        properties.SetProperty("audio_test", 1);    // 音频测试模式
        properties.SetProperty("input_pcm_name", "buweishui_48000_2_s16le.pcm");
        // 麦克风采样属性
        properties.SetProperty("mic_sample_fmt", AV_SAMPLE_FMT_S16);
        properties.SetProperty("mic_sample_rate", 48000);
        properties.SetProperty("mic_channels", 2);
        // 音频编码属性
        properties.SetProperty("audio_sample_rate", 48000);
        properties.SetProperty("audio_bitrate", 64*1024);
        properties.SetProperty("audio_channels", 2);

        // 视频test模式
        properties.SetProperty("video_test", 1);    // 视频测试模式
        properties.SetProperty("input_yuv_name", "720x480_25fps_420p.yuv");
        // 桌面录制属性
        properties.SetProperty("desktop_x", 0);
        properties.SetProperty("desktop_y", 0);
        properties.SetProperty("desktop_width", 720);
        properties.SetProperty("desktop_height", 480);
        properties.SetProperty("desktop_pixel_format", AV_PIX_FMT_YUV420P);
        properties.SetProperty("desktop_fps", 15);
        
        // 视频编码属性
        properties.SetProperty("video_bitrate", 512*1024);  // 设置码率

        properties.SetProperty("rtsp_url", RTSP_URL);
        properties.SetProperty("rtsp_trantport", "udp");
        if(push_work.Init(properties) != RET_OK) {
            LogError("PushWork init failed");
            return -1;
        }

        int count = 0;
        while (true) {
            LogInfo("%d",count);
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            if(count++ > 10)
                break;
        }

    }
    LogInfo("main finish!");
    return 0;
}
