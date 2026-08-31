#ifndef CAR_MANAGER_H
#define CAR_MANAGER_H

#include <vector>
#include <thread>
#include <mutex>
#include "car.h"

// Owns the enemy cars and the thread that moves them.
//
// Design 2 (single update thread): one thread iterates over every car each
// tick, then spawns a wave and removes off-screen cars.
//
// server.cpp only calls start(), stop() and getCarStates().
class CarManager {
public:
    CarManager();
    ~CarManager();

    void start();   // create the update thread and begin moving cars
    void stop();     // stop the thread and join it

    std::vector<Car> getCarStates();  // a copy of every car, safe to read from another thread

private:
    bool running;

    std::vector<Car> cars;
    std::mutex carsMutex;
    int nextCarId;
    int waveCounter;

    std::thread updateThread;
    void updateLoop();

    void seedInitialCars();

    // The functions below must be called with carsMutex already locked.

    // Per-car movement logic (identical on every design branch).
    void stepCarUnlocked(Car& c);          // move one car + maybe change lane
    bool carAheadTooClose(const Car& c);   // another car right in front, same lane
    bool isTopmostInBand(const Car& c);
    bool laneHasRoom(const Car& c, int lane);
    bool wouldCreateWallUnlocked(int excludeCarId, int lane, int yBand);

    // Wave spawning + cleanup.
    bool laneIsFreeAtTop(int lane);
    void spawnWaveUnlocked();
    void removeOffscreenCarsUnlocked();
};

#endif
