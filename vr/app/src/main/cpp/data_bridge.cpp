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

bool DataBridge::Send(const char* line) {
    if (sock_ < 0) return false;
    std::string cmd = std::string(line) + "\n";
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

bool DataBridge::Poll(std::string& type, std::vector<std::string>& fields) {
    std::string line;
    if (!ReadLine(line)) return false;

    fields.clear();
    size_t sp = line.find(' ');
    type = (sp == std::string::npos) ? line : line.substr(0, sp);
    if (sp == std::string::npos) return true;  // no args (e.g. READY/DONE/CLOSE)

    std::string rest = line.substr(sp + 1);
    size_t start = 0;
    while (true) {
        size_t bar = rest.find('|', start);
        if (bar == std::string::npos) { fields.push_back(rest.substr(start)); break; }
        fields.push_back(rest.substr(start, bar - start));
        start = bar + 1;
    }
    return true;
}

void DataBridge::Disconnect() {
    if (sock_ >= 0) { ::close(sock_); sock_ = -1; }
}
