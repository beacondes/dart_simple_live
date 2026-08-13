#pragma once
#include <atomic>
#include <string>
#include <thread>

#include <jni.h>

#include "flv_demuxer.h"
#include "video_decoder.h"

// Downloads an HTTP-FLV live stream on a background thread, demuxes H.264,
// and feeds the decoded NALs into the VideoDecoder.
class StreamDownloader {
public:
    StreamDownloader() = default;
    ~StreamDownloader();

    bool Start(const char* url, JavaVM* vm, VideoDecoder* decoder);
    void Stop();
    bool IsRunning() const { return running_; }

private:
    void Run();

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::string url_;
    JavaVM* vm_ = nullptr;
    VideoDecoder* decoder_ = nullptr;
    FlvDemuxer demuxer_;
    int sock_ = -1;
};
