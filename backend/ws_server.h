#ifndef WS_SERVER_H
#define WS_SERVER_H

#include <string>
#include <thread>
#include <mutex>
#include <vector>

// Small WebSocket server. It only sends text messages to the browser
// (server -> client). Incoming data is ignored, except ping and close.
// No TLS: connect with ws://, not wss://.
class WsServer {
public:
    WsServer();
    ~WsServer();

    bool start(int port);   // start listening, returns false on error
    void stop();

    void broadcastText(const std::string& message);  // send to every client
    int clientCount();

private:
    void acceptLoop();
    bool handshake(int fd);              // HTTP Upgrade handshake
    void handleIncoming(int fd, bool& keepOpen);  // react to ping/close
    void dropClient(int fd);

    int listenFd;
    bool running;
    std::thread acceptThread;

    std::mutex clientsMutex;
    std::vector<int> clients;
};

#endif
