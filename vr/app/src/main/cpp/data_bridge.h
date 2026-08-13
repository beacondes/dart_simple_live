#pragma once
#include <string>
#include <vector>

// TCP client that talks to the Dart bridge server (see bridge/).
// Protocol: newline-delimited text lines. Each line is TYPE + space + fields
// separated by '|'. Examples:
//   client -> server: WATCH <platform> <roomId> | LIST <platform> | SEARCH <platform> <kw>
//   server -> client: STREAM <url> | DANMAKU <user>|<text> | ROOM <id>|<title>|<user>|<online>
//                     | READY | CLOSE | DONE | ERROR <msg>
class DataBridge {
public:
    DataBridge() = default;
    ~DataBridge();

    bool Connect(const char* host, int port);
    // Send an arbitrary command line (newline is appended automatically).
    bool Send(const char* line);
    // Non-blocking: parse one pending line into type + pipe-separated fields.
    bool Poll(std::string& type, std::vector<std::string>& fields);
    bool IsConnected() const { return sock_ >= 0; }
    void Disconnect();

private:
    bool ReadLine(std::string& line);
    int sock_ = -1;
    std::string buffer_;
};
