#include <csignal>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <thread>
#include <chrono>

#include "car_manager.h"
#include "protocol.h"
#include "ws_server.h"

// Entry point: build the world, start the update thread, and send a JSON
// snapshot to every connected browser about 16 times per second.

static const int DEFAULT_PORT = 5000;
static const int DESIGN_NUMBER = 2;   // this branch runs design 2
static const int BROADCAST_MS = 60;

static volatile bool stopRequested = false;

static void onSignal(int) {
    stopRequested = true;
}

int main() {
    std::cout << std::unitbuf;  // flush logs right away (visible in `docker logs`)
    std::srand((unsigned)std::time(nullptr));
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    std::signal(SIGPIPE, SIG_IGN);

    int port = DEFAULT_PORT;
    if (std::getenv("PORT")) {
        port = std::atoi(std::getenv("PORT"));
    }

    std::cout << "Backend starting (design " << DESIGN_NUMBER << ")...\n";

    CarManager manager;
    manager.start();

    WsServer ws;
    if (!ws.start(port)) {
        manager.stop();
        return 1;
    }

    long tick = 0;
    while (!stopRequested) {
        std::string message = serialize(manager.getCarStates(), tick, DESIGN_NUMBER);
        ws.broadcastText(message);
        tick++;
        std::this_thread::sleep_for(std::chrono::milliseconds(BROADCAST_MS));
    }

    std::cout << "\nShutting down...\n";
    ws.stop();
    manager.stop();
    return 0;
}
