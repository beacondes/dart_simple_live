#pragma once
#include <string>

// TCP client that talks to the Dart bridge server (see bridge/).
// Protocol: newline-delimited text lines.
//   client -> server: "WATCH <platform> <roomId>"
//   server -> client: "STREAM <url>" / "DANMAKU <user>|<text>" / "READY" / "CLOSE" / "ERROR <msg>"
class DataBridge {
public:
    DataBridge() = default;
    ~DataBridge();

    bool Connect(const char* host, int port);
    bool SendWatch(const char* platform, const char* roomId);
    bool Poll(std::string& type, std::string& a, std::string& b);
    bool IsConnected() const { return sock_ >= 0; }
    void Disconnect();

private:
    bool ReadLine(std::string& line);
    int sock_ = -1;
    std::string buffer_;
};
