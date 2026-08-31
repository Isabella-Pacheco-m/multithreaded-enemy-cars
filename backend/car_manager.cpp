#include "car_manager.h"

#include <chrono>
#include <cstdlib>

// A car keeps at least this gap from another car in the same lane.
static const int MIN_GAP = 150;
// Height of the window that must always keep one lane open (room to dodge).
static const int WALL_BAND = 250;
// A new car only spawns if its lane is clear this far from the top.
static const int SPAWN_GAP = 150;
// Most cars alive at once.
static const int MAX_CARS = 5;
// Cars only change lane above this y, so they commit before reaching the player.
static const int MERGE_ZONE_Y = 250;
// Ticks between waves: ~20 * 100ms = every 2s.
static const int WAVE_EVERY = 20;
// Past this y a car left the screen. A bit below the frontend height (840).
static const int OFFSCREEN_Y = 900;
static const int TICK_MS = 100;

CarManager::CarManager() {
    running = false;
    seedInitialCars();
}

CarManager::~CarManager() {
    stop();
}

void CarManager::seedInitialCars() {
    cars.clear();
    nextCarId = 0;
    waveCounter = WAVE_EVERY;   // first wave right away

    for (int i = 0; i < 3; i++) {
        Car car(nextCarId);
        car.setLane(i % NUM_LANES);
        car.setY(-i * 280);   // well spread so they never start as a wall
        cars.push_back(car);
        nextCarId++;
    }
}

// ---- shared movement logic (same on every design) ----

// Another car right in front of `c` in the same lane?
bool CarManager::carAheadTooClose(const Car& c) {
    bool tooClose = false;
    for (int i = 0; i < (int)cars.size(); i++) {
        bool other = cars[i].getId() != c.getId();
        bool sameLane = cars[i].getPosition().lane == c.getPosition().lane;
        if (other && sameLane) {
            int gap = cars[i].getPosition().y - c.getPosition().y;
            if (gap > 0 && gap < MIN_GAP) {
                tooClose = true;
            }
        }
    }
    return tooClose;
}

// Is there room in `lane` for car `c` (no other car within MIN_GAP, front or back)?
bool CarManager::laneHasRoom(const Car& c, int lane) {
    bool room = true;
    for (int i = 0; i < (int)cars.size(); i++) {
        bool other = cars[i].getId() != c.getId();
        bool inLane = cars[i].getPosition().lane == lane;
        if (other && inLane) {
            int gap = cars[i].getPosition().y - c.getPosition().y;
            if (gap > -MIN_GAP && gap < MIN_GAP) {
                room = false;
            }
        }
    }
    return room;
}

// Would a car in `lane` around `yBand` leave every lane blocked in that band?
bool CarManager::wouldCreateWallUnlocked(int excludeCarId, int lane, int yBand) {
    bool blocked[NUM_LANES];
    for (int i = 0; i < NUM_LANES; i++) {
        blocked[i] = false;
    }
    blocked[lane] = true;

    for (int i = 0; i < (int)cars.size(); i++) {
        bool counts = cars[i].getId() != excludeCarId &&
                      std::abs(cars[i].getPosition().y - yBand) < WALL_BAND;
        if (counts) {
            blocked[cars[i].getPosition().lane] = true;
        }
    }

    bool allBlocked = true;
    for (int i = 0; i < NUM_LANES; i++) {
        if (!blocked[i]) {
            allBlocked = false;
        }
    }
    return allBlocked;
}

// True if no other car in the same y band is higher up (closer to the top).
bool CarManager::isTopmostInBand(const Car& c) {
    bool topmost = true;
    for (int i = 0; i < (int)cars.size(); i++) {
        bool other = cars[i].getId() != c.getId();
        int dy = cars[i].getPosition().y - c.getPosition().y;
        bool aboveInBand = dy < 0 && dy > -WALL_BAND;
        if (other && aboveInBand) {
            topmost = false;
        }
    }
    return topmost;
}

void CarManager::stepCarUnlocked(Car& c) {
    int beforeY = c.getPosition().y;

    // Move down unless the car in front is too close (no rear-end crashes).
    if (!carAheadTooClose(c)) {
        c.move();
        // If this car would sit in a band with all 3 lanes taken, the one
        // furthest back waits a tick so a gap stays open across the road.
        bool walls = wouldCreateWallUnlocked(c.getId(), c.getPosition().lane, c.getPosition().y);
        if (walls && isTopmostInBand(c)) {
            c.setY(beforeY);
        }
    }

    // Lane changes: only near the top, only by one lane, only into a clear lane.
    int proposed = c.proposeLane();
    bool nearTop = c.getPosition().y <= MERGE_ZONE_Y;
    bool wantsChange = proposed != c.getPosition().lane;
    bool safeChange = nearTop && wantsChange &&
                      laneHasRoom(c, proposed) &&
                      !wouldCreateWallUnlocked(c.getId(), proposed, c.getPosition().y);
    if (safeChange) {
        c.setLane(proposed);
    }
}

// ---- wave spawning + cleanup ----

bool CarManager::laneIsFreeAtTop(int lane) {
    bool free = true;
    for (int i = 0; i < (int)cars.size(); i++) {
        bool sameLane = cars[i].getPosition().lane == lane;
        bool nearTop = cars[i].getPosition().y < SPAWN_GAP;
        if (sameLane && nearTop) {
            free = false;
        }
    }
    return free;
}

void CarManager::spawnWaveUnlocked() {
    if ((int)cars.size() < MAX_CARS) {
        // Spawn in up to 2 lanes, always leaving one open: a wave is never a wall.
        int openLane = rand() % NUM_LANES;
        for (int lane = 0; lane < NUM_LANES; lane++) {
            bool canSpawnHere = (lane != openLane) && laneIsFreeAtTop(lane);
            if (canSpawnHere) {
                Car car(nextCarId);
                car.setLane(lane);
                cars.push_back(car);
                nextCarId++;
            }
        }
    }
}

void CarManager::removeOffscreenCarsUnlocked() {
    std::vector<Car> keep;
    for (int i = 0; i < (int)cars.size(); i++) {
        if (cars[i].getPosition().y <= OFFSCREEN_Y) {
            keep.push_back(cars[i]);
        }
    }
    cars = keep;
}

std::vector<Car> CarManager::getCarStates() {
    carsMutex.lock();
    std::vector<Car> copy = cars;
    carsMutex.unlock();
    return copy;
}

// ---- design 2: one thread updates every car ----

void CarManager::start() {
    running = true;
    updateThread = std::thread(&CarManager::updateLoop, this);
}

void CarManager::stop() {
    running = false;
    if (updateThread.joinable()) {
        updateThread.join();
    }
}

void CarManager::updateLoop() {
    while (running) {
        carsMutex.lock();

        for (int i = 0; i < (int)cars.size(); i++) {
            stepCarUnlocked(cars[i]);
        }

        waveCounter++;
        if (waveCounter >= WAVE_EVERY) {
            spawnWaveUnlocked();
            waveCounter = 0;
        }
        removeOffscreenCarsUnlocked();

        carsMutex.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(TICK_MS));
    }
}
