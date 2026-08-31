#ifndef CAR_MANAGER_H
#define CAR_MANAGER_H

#include <vector>
#include <thread>
#include <mutex>
#include "car.h"

// Owns the enemy cars and the threads that move them.
//
// Design 1 (one thread per car): a fixed fleet of DESIGN1_CARS cars, each
// with its own std::thread. When a car drives off screen its own thread
// respawns it in a free lane (the "safe" one-at-a-time spawner). Adding a
// car means adding a thread, which is why the fleet is kept small.
//
// server.cpp only calls start(), stop() and getCarStates().
class CarManager {
public:
    CarManager();
    ~CarManager();

    void start();   // create one thread per car and begin moving them
    void stop();     // stop the threads and join them

    std::vector<Car> getCarStates();  // a copy of every car, safe to read from another thread

private:
    bool running;

    std::vector<Car> cars;   // fixed size after seedInitialCars(): never resized
    std::mutex carsMutex;
    int nextCarId;

    std::vector<std::thread> carThreads;
    void oneCarLoop(int index);   // one per car: moves cars[index], respawns it when off screen

    void seedInitialCars();

    // The functions below must be called with carsMutex already locked.

    // Per-car movement logic (identical on every design branch).
    void stepCarUnlocked(Car& c);          // move one car + maybe change lane
    bool carAheadTooClose(const Car& c);   // another car right in front, same lane
    bool isTopmostInBand(const Car& c);
    bool laneHasRoom(const Car& c, int lane);
    bool wouldCreateWallUnlocked(int excludeCarId, int lane, int yBand);

    // Design 1 spawner: pick a lane with room at the top for a fresh car.
    Position generateSafeStartPosition();
};

#endif
