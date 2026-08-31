#include "ws_server.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <cstdint>
#include <cstring>
#include <iostream>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

// The WebSocket handshake requires SHA-1 + base64 of the client key.
// This is the standard math from RFC 6455 / FIPS 180-1; you can treat it
// as a black box. The interesting threading code is further down.
namespace {

struct Sha1 {
    uint32_t h[5];
    uint64_t length = 0;
    unsigned char buf[64];
    size_t bufLen = 0;

    Sha1() {
        h[0] = 0x67452301; h[1] = 0xEFCDAB89; h[2] = 0x98BADCFE;
        h[3] = 0x10325476; h[4] = 0xC3D2E1F0;
    }
    static uint32_t rol(uint32_t v, int b) { return (v << b) | (v >> (32 - b)); }

    void block(const unsigned char* p) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = (p[i*4] << 24) | (p[i*4+1] << 16) | (p[i*4+2] << 8) | p[i*4+3];
        for (int i = 16; i < 80; i++)
            w[i] = rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

        uint32_t a=h[0], b=h[1], c=h[2], d=h[3], e=h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | (~b & d);            k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d;                     k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d);   k = 0x8F1BBCDC; }
            else             { f = b ^ c ^ d;                     k = 0xCA62C1D6; }
            uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e;
    }

    void update(const unsigned char* data, size_t len) {
        length += len;
        while (len > 0) {
            size_t take = 64 - bufLen;
            if (take > len) take = len;
            memcpy(buf + bufLen, data, take);
            bufLen += take; data += take; len -= take;
            if (bufLen == 64) { block(buf); bufLen = 0; }
        }
    }

    void finish(unsigned char out[20]) {
        uint64_t bits = length * 8;
        unsigned char one = 0x80, zero = 0x00;
        update(&one, 1);
        while (bufLen != 56) update(&zero, 1);
        unsigned char lenBytes[8];
        for (int i = 0; i < 8; i++) lenBytes[i] = (unsigned char)(bits >> (56 - i*8));
        update(lenBytes, 8);
        for (int i = 0; i < 5; i++) {
            out[i*4]   = (unsigned char)(h[i] >> 24);
            out[i*4+1] = (unsigned char)(h[i] >> 16);
            out[i*4+2] = (unsigned char)(h[i] >> 8);
            out[i*4+3] = (unsigned char)(h[i]);
        }
    }
};

std::string base64(const unsigned char* data, size_t len) {
    static const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = data[i] << 16;
        if (i + 1 < len) n |= data[i+1] << 8;
        if (i + 2 < len) n |= data[i+2];
        out += tbl[(n >> 18) & 63];
        out += tbl[(n >> 12) & 63];
        out += (i + 1 < len) ? tbl[(n >> 6) & 63] : '=';
        out += (i + 2 < len) ? tbl[n & 63] : '=';
    }
    return out;
}

std::string acceptKey(const std::string& clientKey) {
    std::string s = clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    Sha1 sha;
    sha.update((const unsigned char*)s.data(), s.size());
    unsigned char digest[20];
    sha.finish(digest);
    return base64(digest, 20);
}

// --- small socket helpers ---

std::string readRequest(int fd) {
    std::string req;
    char chunk[1024];
    while (req.size() < 8192) {
        long n = recv(fd, chunk, sizeof(chunk), 0);
        if (n <= 0) break;
        req.append(chunk, n);
        if (req.find("\r\n\r\n") != std::string::npos) break;
    }
    return req;
}

std::string headerValue(const std::string& req, std::string name) {
    std::string low = req;
    for (char& c : low) c = (char)tolower((unsigned char)c);
    for (char& c : name) c = (char)tolower((unsigned char)c);

    size_t pos = low.find(name + ":");
    if (pos == std::string::npos) return "";
    pos += name.size() + 1;
    size_t end = req.find("\r\n", pos);
    std::string v = req.substr(pos, end - pos);
    size_t a = v.find_first_not_of(" \t");
    size_t b = v.find_last_not_of(" \t");
    if (a == std::string::npos) return "";
    return v.substr(a, b - a + 1);
}

bool sendAll(int fd, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        long n = send(fd, data + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

// Wrap a payload in one unmasked frame with the given opcode.
std::string buildFrame(int opcode, const std::string& payload) {
    std::string f;
    f += (char)(0x80 | opcode);
    size_t len = payload.size();
    if (len < 126) {
        f += (char)len;
    } else if (len <= 0xFFFF) {
        f += (char)126;
        f += (char)((len >> 8) & 0xFF);
        f += (char)(len & 0xFF);
    } else {
        f += (char)127;
        for (int i = 7; i >= 0; i--) f += (char)((len >> (i * 8)) & 0xFF);
    }
    f += payload;
    return f;
}

}  // namespace


WsServer::WsServer() {
    listenFd = -1;
    running = false;
}

WsServer::~WsServer() {
    stop();
}

bool WsServer::start(int port) {
    listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd < 0) {
        std::cerr << "[ws] socket() failed\n";
        return false;
    }

    int yes = 1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(listenFd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[ws] bind() failed on port " << port << "\n";
        close(listenFd);
        listenFd = -1;
        return false;
    }
    if (listen(listenFd, 16) < 0) {
        std::cerr << "[ws] listen() failed\n";
        close(listenFd);
        listenFd = -1;
        return false;
    }

    running = true;
    acceptThread = std::thread(&WsServer::acceptLoop, this);
    std::cout << "[ws] listening on ws://0.0.0.0:" << port << "\n";
    return true;
}

void WsServer::stop() {
    if (running) {
        running = false;

        if (listenFd >= 0) {
            shutdown(listenFd, SHUT_RDWR);
            close(listenFd);
            listenFd = -1;
        }
        if (acceptThread.joinable()) {
            acceptThread.join();
        }

        clientsMutex.lock();
        for (int i = 0; i < (int)clients.size(); i++) {
            close(clients[i]);
        }
        clients.clear();
        clientsMutex.unlock();
    }
}

void WsServer::acceptLoop() {
    while (running) {
        int fd = accept(listenFd, NULL, NULL);

        bool accepted = (fd >= 0) && handshake(fd);
        if (fd >= 0 && !accepted) {
            close(fd);
        }

        if (accepted) {
            // Client -> server reads are best-effort: make the socket non-blocking.
            int flags = fcntl(fd, F_GETFL, 0);
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);

            clientsMutex.lock();
            clients.push_back(fd);
            int total = (int)clients.size();
            clientsMutex.unlock();

            std::cout << "[ws] client connected (" << total << " total)\n";
        }
    }
}

bool WsServer::handshake(int fd) {
    std::string req = readRequest(fd);
    std::string key = headerValue(req, "Sec-WebSocket-Key");
    if (key.empty()) {
        return false;
    }

    std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + acceptKey(key) + "\r\n\r\n";

    return sendAll(fd, response.data(), response.size());
}

void WsServer::handleIncoming(int fd, bool& keepOpen) {
    keepOpen = true;
    unsigned char buf[2048];
    long n = recv(fd, buf, sizeof(buf), 0);
    if (n == 0) {
        keepOpen = false;   // client closed the socket
        return;
    }
    if (n < 0) {
        return;             // nothing waiting
    }

    long i = 0;
    while (i + 2 <= n) {
        int opcode = buf[i] & 0x0F;
        bool masked = (buf[i + 1] & 0x80) != 0;
        long len = buf[i + 1] & 0x7F;
        long header = 2;

        if (len == 126) {
            if (i + 4 > n) break;
            len = (buf[i + 2] << 8) | buf[i + 3];
            header = 4;
        } else if (len == 127) {
            break;  // we never expect frames this large from the browser
        }

        long maskLen = masked ? 4 : 0;
        if (i + header + maskLen + len > n) break;

        unsigned char* mask = buf + i + header;
        unsigned char* payload = buf + i + header + maskLen;
        if (masked) {
            for (long k = 0; k < len; k++) payload[k] ^= mask[k % 4];
        }

        if (opcode == 0x8) {            // close
            keepOpen = false;
            return;
        } else if (opcode == 0x9) {     // ping -> pong
            std::string pong = buildFrame(0xA, std::string((char*)payload, len));
            sendAll(fd, pong.data(), pong.size());
        }
        // text / binary / pong frames are ignored on purpose

        i += header + maskLen + len;
    }
}

void WsServer::dropClient(int fd) {
    close(fd);
    clientsMutex.lock();
    for (int i = 0; i < (int)clients.size(); i++) {
        if (clients[i] == fd) {
            clients.erase(clients.begin() + i);
            break;
        }
    }
    clientsMutex.unlock();
}

void WsServer::broadcastText(const std::string& message) {
    std::string frame = buildFrame(0x1, message);

    clientsMutex.lock();
    std::vector<int> current = clients;
    clientsMutex.unlock();

    for (int i = 0; i < (int)current.size(); i++) {
        int fd = current[i];

        bool keepOpen = true;
        handleIncoming(fd, keepOpen);

        bool sent = keepOpen && sendAll(fd, frame.data(), frame.size());
        if (!sent) {
            dropClient(fd);
            std::cout << "[ws] client disconnected\n";
        }
    }
}

int WsServer::clientCount() {
    clientsMutex.lock();
    int n = (int)clients.size();
    clientsMutex.unlock();
    return n;
}
