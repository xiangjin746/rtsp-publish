#include "commonlooper.h"
#include "dlog.h"

CommonLooper::CommonLooper():
    request_abort_(false),
    running_(false)
{

}

CommonLooper::~CommonLooper()
{
    Stop();
}

RET_CODE CommonLooper::Start()
{
    LogInfo("info");
    worker_ = new std::thread(&CommonLooper::trampoline, this);
    if(!worker_){
        LogError("new std::this_thread failed");
        return RET_FAIL;
    }
    return RET_OK;
}

void CommonLooper::Stop()
{
    request_abort_ = true;
    if(!worker_){
        worker_->join();
        delete worker_;
        worker_ = NULL;
    }
}

bool CommonLooper::Running()
{
    return running_;
}

void CommonLooper::SetRunning(bool running)
{
    this->running_ = running;
}

void *CommonLooper::trampoline(void *p)
{
    LogInfo("info");
    ((CommonLooper *)p)->SetRunning(true);
    ((CommonLooper *)p)->Loop();
    ((CommonLooper *)p)->SetRunning(false);
    LogInfo("exit");

    return nullptr;
}
