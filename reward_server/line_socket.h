#pragma once
#include <winsock2.h>
#include <string>

namespace rs {

/** Newline-framed reader/writer over one accepted socket; owns and closes the handle. */
class LineSocket {
public:
    static constexpr size_t kMaxLineBytes = 16u << 20;

    explicit LineSocket(SOCKET s) : sock_(s) {}
    ~LineSocket() { if (sock_ != INVALID_SOCKET) closesocket(sock_); }
    LineSocket(const LineSocket&) = delete;
    LineSocket& operator=(const LineSocket&) = delete;

    /** Blocks for the next line without its terminator; false on disconnect or an oversized line. */
    bool RecvLine(std::string& out) {
        for (;;) {
            const size_t nl = buf_.find('\n');
            if (nl != std::string::npos) {
                out = buf_.substr(0, nl);
                buf_.erase(0, nl + 1);
                if (!out.empty() && out.back() == '\r') out.pop_back();
                return true;
            }
            if (buf_.size() > kMaxLineBytes) return false;
            char tmp[16384];
            const int n = recv(sock_, tmp, sizeof(tmp), 0);
            if (n <= 0) return false;
            buf_.append(tmp, n);
        }
    }

    bool SendLine(const std::string& s) {
        std::string line = s;
        line.push_back('\n');
        size_t sent = 0;
        while (sent < line.size()) {
            const int n = send(sock_, line.data() + sent, (int)(line.size() - sent), 0);
            if (n <= 0) return false;
            sent += (size_t)n;
        }
        return true;
    }

private:
    SOCKET sock_;
    std::string buf_;
};

}
