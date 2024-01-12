#include <iostream>
#include "dlog.h"
#include <thread>
using namespace std;

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/audio_fifo.h>
}

using namespace std;

int main()
{
    cout << "Hello World!" << endl;

    init_logger("rtsp_push.log", S_INFO);

    {
        int count = 0;
        while (true) {
            LogInfo("%d",count);
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            if(count++ > 5)
                break;
        }

    }
    LogInfo("main finish!");
    return 0;
}
