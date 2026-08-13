#include "stream_downloader.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

#include <android/log.h>

#define LOG_TAG "StreamDownloader"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static void ParseUrl(const std::string& url, std::string& host, int& port, std::string& path) {
    std::string s = url;
    size_t p = s.find("://");
    if (p != std::string::npos) s = s.substr(p + 3);
    size_t slash = s.find('/');
    std::string authority = (slash == std::string::npos) ? s : s.substr(0, slash);
    path = (slash == std::string::npos) ? "/" : s.substr(slash);
    size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
        host = authority.substr(0, colon);
        port = atoi(authority.substr(colon + 1).c_str());
    } else {
        host = authority;
        port = 80;
    }
}

StreamDownloader::~StreamDownloader() { Stop(); }

bool StreamDownloader::Start(const char* url, JavaVM* vm, VideoDecoder* decoder) {
    if (running_) return false;
    url_ = url;
    vm_ = vm;
    decoder_ = decoder;
    running_ = true;
    thread_ = std::thread(&StreamDownloader::Run, this);
    return true;
}

void StreamDownloader::Stop() {
    if (!running_ && !thread_.joinable()) return;
    running_ = false;
    if (sock_ >= 0) { ::shutdown(sock_, SHUT_RDWR); ::close(sock_); sock_ = -1; }
    if (thread_.joinable()) thread_.join();
}

void StreamDownloader::Run() {
    JNIEnv* env = nullptr;
    bool needDetach = false;
    if (vm_ && vm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        if (vm_->AttachCurrentThread(&env, nullptr) == JNI_OK) needDetach = true;
    }

    demuxer_.SetNaluCallback([this, env](const uint8_t* nalu, size_t len, int64_t pts) {
        if (env && decoder_) decoder_->Feed(env, nalu, len, pts);
    });

    std::string host;
    int port = 80;
    std::string path = "/";
    ParseUrl(url_, host, port, path);

    sock_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock_ < 0) { LOGE("socket failed"); running_ = false; return; }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        LOGE("bad host %s", host.c_str());
        ::close(sock_); sock_ = -1; running_ = false;
        if (needDetach) vm_->DetachCurrentThread();
        return;
    }

    if (::connect(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOGE("connect failed: %s", strerror(errno));
        ::close(sock_); sock_ = -1; running_ = false;
        if (needDetach) vm_->DetachCurrentThread();
        return;
    }

    std::string req = "GET " + path + " HTTP/1.1\r\n" +
                      "Host: " + host + "\r\n" +
                      "User-Agent: SimpleLiveVR/0.1\r\n" +
                      "Accept: */*\r\n" +
                      "Connection: close\r\n\r\n";
    ::send(sock_, req.c_str(), req.size(), 0);
    LOGI("downloading %s", url_.c_str());

    // Read until end of HTTP headers, then stream body through the demuxer.
    std::string header;
    char tmp[8192];
    bool headerDone = false;
    while (running_ && !headerDone) {
        ssize_t n = ::recv(sock_, tmp, sizeof(tmp), 0);
        if (n <= 0) { running_ = false; break; }
        header.append(tmp, n);
        size_t pos = header.find("\r\n\r\n");
        if (pos != std::string::npos) {
            size_t bodyStart = pos + 4;
            if (header.size() > bodyStart) {
                demuxer_.Feed(reinterpret_cast<const uint8_t*>(header.data()) + bodyStart,
                              header.size() - bodyStart);
            }
            headerDone = true;
        }
    }

    while (running_) {
        ssize_t n = ::recv(sock_, tmp, sizeof(tmp), 0);
        if (n <= 0) break;
        demuxer_.Feed(reinterpret_cast<const uint8_t*>(tmp), n);
    }

    ::close(sock_); sock_ = -1;
    running_ = false;
    if (needDetach) vm_->DetachCurrentThread();
    LOGI("stream download finished");
}
