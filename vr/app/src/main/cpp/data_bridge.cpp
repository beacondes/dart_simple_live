#include "data_bridge.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>

#include <android/log.h>

#define LOG_TAG "DataBridge"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

DataBridge::~DataBridge() { Disconnect(); }

bool DataBridge::Connect(const char* host, int port) {
    sock_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock_ < 0) { LOGE("socket failed"); return false; }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        LOGE("inet_pton failed for %s", host);
        Disconnect();
        return false;
    }

    if (::connect(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOGE("connect to %s:%d failed: %s", host, port, strerror(errno));
        Disconnect();
        return false;
    }

    int flags = fcntl(sock_, F_GETFL, 0);
    fcntl(sock_, F_SETFL, flags | O_NONBLOCK);
    LOGI("connected to %s:%d", host, port);
    return true;
}

bool DataBridge::SendWatch(const char* platform, const char* roomId) {
    if (sock_ < 0) return false;
    std::string cmd = std::string("WATCH ") + platform + " " + roomId + "\n";
    ssize_t n = ::send(sock_, cmd.c_str(), cmd.size(), 0);
    return n == static_cast<ssize_t>(cmd.size());
}

bool DataBridge::ReadLine(std::string& line) {
    char tmp[4096];
    ssize_t n = ::recv(sock_, tmp, sizeof(tmp), 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return false;
        LOGE("recv error: %s", strerror(errno));
        Disconnect();
        return false;
    }
    if (n == 0) { LOGE("connection closed by peer"); Disconnect(); return false; }

    buffer_.append(tmp, n);
    size_t pos = buffer_.find('\n');
    if (pos == std::string::npos) return false;
    line = buffer_.substr(0, pos);
    buffer_.erase(0, pos + 1);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return true;
}

bool DataBridge::Poll(std::string& type, std::string& a, std::string& b) {
    std::string line;
    if (!ReadLine(line)) return false;

    size_t sp = line.find(' ');
    type = (sp == std::string::npos) ? line : line.substr(0, sp);
    std::string rest = (sp == std::string::npos) ? "" : line.substr(sp + 1);
    size_t bar = rest.find('|');
    if (bar == std::string::npos) { a = rest; b = ""; }
    else { a = rest.substr(0, bar); b = rest.substr(bar + 1); }
    return true;
}

void DataBridge::Disconnect() {
    if (sock_ >= 0) { ::close(sock_); sock_ = -1; }
}
