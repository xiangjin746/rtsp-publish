#ifndef PACKETQUEUE_H
#define PACKETQUEUE_H



#include <mutex>
#include <condition_variable>
#include <queue>
#include "mediabase.h"
#include "dlog.h"

extern "C"
{
#include "libavcodec/avcodec.h"
}

typedef struct packet_queue_stats {
    int audio_nb_packets;   // 音频包数量
    int video_nb_packets;   // 视频包数量
    int audio_size;         // 音频总大小 字节
    int video_size;         // 视频总大小 字节
    int64_t audio_duration; //音频持续时长
    int64_t video_duration; //视频持续时长
}PacketQueueStats;

typedef struct my_avpacket {
    AVPacket *packet;
    MediaType media_type;
}MyAVPacket;

class PacketQueue
{
public:
    PacketQueue(double audio_frame_duration, double video_frame_duration):
        audio_frame_duration_(audio_frame_duration),
        video_frame_duration_(video_frame_duration)
    {
        if(audio_frame_duration_ < 0){
            audio_frame_duration_ = 0;
        }
        if(video_frame_duration_ < 0){
            video_frame_duration_ = 0;
        }
        memset(&stats_, 0, sizeof(PacketQueueStats));
    }
    ~PacketQueue(){

    }

    // 数据包的入队操作 - Push 方法
    int Push(AVPacket *pkt,MediaType media_type) {
        //step 1: 参数检查
        if(!pkt) {
            LogError("pkt is null");
            return -1;
        }

        if(media_type != E_AUDIO_TYPE && media_type != E_VIDEO_TYPE) {
            LogError("media_type:%d is unknown", media_type);
            return -1;
        }

        //step 2:加锁和调用pushPrivate操作
        std::lock_guard<std::mutex> lock(mutex_);
        int ret = pushPrivate(pkt,media_type);
        if(ret < 0) {
            LogError("pushPrivate failed");
            return -1;
        }else{
            cond_.notify_one();
            return 0;
        }
    }

    // 数据包的入队操作 - pushPrivate 方法
    int pushPrivate(AVPacket *pkt,MediaType media_type) {
        // 步骤 1: 检查是否有中断请求
        if(abort_request_) { 
            LogWarn("abort request");
            return -1;
        }
        // 步骤 2: 为 MyAVPacket 结构体分配内存
        MyAVPacket *mypkt = (MyAVPacket *)malloc(sizeof(MyAVPacket));
        if(!mypkt) {
            LogError("malloc MyAVPacket failed");
            return -1;
        }
        mypkt->media_type = media_type;
        mypkt->packet = pkt;
        // 步骤 3: 根据媒体类型更新统计信息
        if(media_type == E_AUDIO_TYPE) {
            stats_.audio_nb_packets++;
            stats_.audio_size += pkt->size;
            audio_front_pts_ = pkt->pts;
        }
        if(media_type == E_VIDEO_TYPE) {
            stats_.video_nb_packets++;
            stats_.video_size += pkt->size;
            video_front_pts_  = pkt->pts;
        }

        // 步骤 4: 将处理好的包加入队列
        queue_.push(mypkt);
        return 0;
    }

    //  数据包的出队操作 - Pop 和 PopWithTimeout 方法
    int Pop(AVPacket **pkt,MediaType &media_type) {
        // 步骤 1: 参数检查和锁定互斥量
        if(!pkt) {
            LogError("pkt is null");
            return -1;
        }
        std::unique_lock<std::mutex> lock(mutex_);
        // 步骤 2: 检查中断请求并等待条件满足
        if(abort_request_) { 
            LogWarn("abort request");
            return -1;
        }
        if(queue_.empty()) {
            cond_.wait(lock,[this] {
                return !queue_.empty() || abort_request_;
            });
        }
        // 再次检查中断请求
        if(abort_request_) {
            LogWarn("abort request");
            return -1;
        }

        // 步骤 3: 处理队列中的数据包
        MyAVPacket *mypkt = queue_.front();
        *pkt        = mypkt->packet;
        media_type  = mypkt->media_type;

        if(media_type == E_AUDIO_TYPE) {
            stats_.audio_nb_packets--;
            stats_.audio_size -= mypkt->packet->size;
            audio_front_pts_ = mypkt->packet->pts;
        }
        if(media_type == E_VIDEO_TYPE) {
            stats_.video_nb_packets--;
            stats_.video_size -= mypkt->packet->size;
            video_front_pts_ = mypkt->packet->pts;
        }
        av_packet_free(&mypkt->packet);
        queue_.pop();
        free(mypkt);
        return 0;
    }

    int PopWithTimeout(AVPacket **pkt, MediaType &media_type, int timeout) {
        // 步骤 1: 如果超时时间为负数，则直接调用 Pop 函数
        if(timeout < 0) {
            return Pop(pkt, media_type);
        }

        // 步骤 2: 使用 unique_lock 加锁
        std::unique_lock<std::mutex> lock(mutex_);

        // 检查是否有中断请求
        if(abort_request_) {
            LogWarn("abort request");
            return -1;
        }

        // 如果队列为空，则等待条件变量，但带有超时时间
        if(queue_.empty()) {
            cond_.wait_for(lock, std::chrono::milliseconds(timeout), [this] {
                return !queue_.empty() || abort_request_;
            });
        }

        // 再次检查中断请求
        if(abort_request_) {
            LogWarn("abort request");
            return -1;
        }

        // 步骤 3: 从队列中取出数据包并更新统计信息
        MyAVPacket *mypkt = queue_.front(); // 读取队列首部元素
        *pkt        = mypkt->packet;
        media_type  = mypkt->media_type;

        // 根据媒体类型更新统计信息
        if(E_AUDIO_TYPE == media_type) {
            stats_.audio_nb_packets--;      // 减少音频包计数
            stats_.audio_size -= mypkt->packet->size;
            audio_front_pts_ = mypkt->packet->pts;
        }
        if(E_VIDEO_TYPE == media_type) {
            stats_.video_nb_packets--;      // 减少视频包计数
            stats_.video_size -= mypkt->packet->size;
            video_front_pts_ = mypkt->packet->pts;
        }

        // 移除队列首部元素并释放内存
        av_packet_free(&mypkt->packet);
        queue_.pop();
        free(mypkt);

        return 0;
    }

    bool Empty() {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    // 唤醒在等待的线程
    void Abort() {
        std::lock_guard<std::mutex> lock(mutex_);
        abort_request_ = true;
        cond_.notify_all();
    }

    // all为true:清空队列;
    // all为false: drop数据，直到遇到I帧, 最大保留remain_max_duration时长;
    int Drop(bool all, int64_t remain_max_duration) {
        // 步骤 1: 加锁和初始化循环
        std::lock_guard<std::mutex> lock(mutex_);

        while (!queue_.empty()) {
            MyAVPacket *mypkt = queue_.front();
            // 步骤 2: 处理非“全部删除”情况
            if (!all && mypkt->media_type == E_VIDEO_TYPE && (mypkt->packet->flags & AV_PKT_FLAG_KEY))
            {
                int64_t duration = video_back_pts_ - video_front_pts_; // 以 pts 为准计算持续时间

                // 检查 PTS 回绕或持续时间异常
                if (duration < 0 || duration > video_frame_duration_ * stats_.video_nb_packets * 2)
                {
                    duration = video_frame_duration_ * stats_.video_nb_packets;
                }
                LogInfo("video duration:%lld", duration);
                // 如果持续时间小于等于最大保留时长，则退出循环
                if(duration <= remain_max_duration)
                    break;
            }

            // 步骤 3: 更新统计信息并删除数据包
            if(E_AUDIO_TYPE == mypkt->media_type) {
                stats_.audio_nb_packets--;      // 减少音频包计数
                stats_.audio_size -= mypkt->packet->size;
                audio_front_pts_ = mypkt->packet->pts;
            }
            if(E_VIDEO_TYPE == mypkt->media_type) {
                stats_.video_nb_packets--;      // 减少视频包计数
                stats_.video_size -= mypkt->packet->size;
                video_front_pts_ = mypkt->packet->pts;
            }

            // 释放 AVPacket
            av_packet_free(&mypkt->packet);
            // 从队列中移除并释放 MyAVPacket
            queue_.pop();
            free(mypkt);
        }

        return 0;
    }

    int64_t GetAudioDuration() {
        std::lock_guard<std::mutex> lock(mutex_);
        int64_t duration = audio_back_pts_ - audio_front_pts_;
        // 也参考帧（包）持续 *帧(包)数
        if(duration < 0     // pts回绕
                || duration > audio_frame_duration_ * stats_.audio_nb_packets * 2) {
            duration =  audio_frame_duration_ * stats_.audio_nb_packets;
        }
        return duration;
    }

    int64_t GetVideoDuration() {
        std::lock_guard<std::mutex> lock(mutex_);
        int64_t duration = video_back_pts_ - video_front_pts_;  //以pts为准
        // 也参考帧（包）持续 *帧(包)数
        if(duration < 0     // pts回绕
                || duration > video_frame_duration_ * stats_.video_nb_packets * 2) {
            duration =  video_frame_duration_ * stats_.video_nb_packets;
        }
        return duration;
    }

    int GetAudioPackets() {
        std::lock_guard<std::mutex> lock(mutex_);
         return stats_.audio_nb_packets;
    }

    int GetVideoPackets() {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_.video_nb_packets;
    }

    void GetStats(PacketQueueStats *stats) {
        if(!stats) {
            LogError("stats is null");
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);

        int64_t audio_duration = audio_back_pts_ - audio_front_pts_;  //以pts为准
        // 也参考帧（包）持续 *帧(包)数
        if(audio_duration < 0     // pts回绕
                || audio_duration > audio_frame_duration_ * stats_.audio_nb_packets * 2) {
            audio_duration =  audio_frame_duration_ * stats_.audio_nb_packets;
        }
        int64_t video_duration = video_back_pts_ - video_front_pts_;  //以pts为准
        // 也参考帧（包）持续 *帧(包)数
        if(video_duration < 0     // pts回绕
                || video_duration > video_frame_duration_ * stats_.video_nb_packets * 2) {
            video_duration =  video_frame_duration_ * stats_.video_nb_packets;
        }

        stats->audio_duration   = audio_duration;
        stats->audio_nb_packets = stats_.audio_nb_packets;
        stats->audio_size       = stats_.audio_size;
        stats->video_duration   = video_duration;
        stats->video_nb_packets = stats_.video_nb_packets;
        stats->video_size       = stats_.video_size;
    }

private:
    std::mutex mutex_;
    std::condition_variable cond_;
    std::queue<MyAVPacket *> queue_;

    bool abort_request_ = false;

    // 统计相关
    PacketQueueStats stats_;
    double audio_frame_duration_ = 23.21995649; // 默认23.2ms 44.1khz  1024*1000ms/44100=23.21995649ms
    double video_frame_duration_ = 40;  // 40ms 视频帧率为25的  ， 1000ms/25=40ms
    // pts记录
    int64_t audio_front_pts_ = 0;
    int64_t audio_back_pts_ = 0;
    int64_t video_front_pts_ = 0;
    int64_t video_back_pts_ = 0;
};



#endif // PACKETQUEUE_H
