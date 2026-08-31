#include "car_manager.h"

#include <chrono>
#include <cstdlib>

// A car keeps at least this gap from another car in the same lane.
static const int MIN_GAP = 150;
// Height of the window that must always keep one lane open (room to dodge).
static const int WALL_BAND = 250;
// Number of cars in the fleet (one thread each).
static const int DESIGN1_CARS = 5;
// Cars only change lane above this y, so they commit before reaching the player.
static const int MERGE_ZONE_Y = 250;
// Past this y a car left the screen and its thread respawns it.
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

    for (int i = 0; i < DESIGN1_CARS; i++) {
        Car car(nextCarId);
        car.setLane(i % NUM_LANES);
        car.setY(-i * 200);   // well spread so they never start as a wall
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

// Design 1 spawner: pick the lane with the most room at the top and place the
// car just behind that lane's nearest car (so it never lands on top of one).
Position CarManager::generateSafeStartPosition() {
    int bestLane = 0;
    int bestNearestY = -1;

    for (int lane = 0; lane < NUM_LANES; lane++) {
        int nearestY = OFFSCREEN_Y + 1;
        for (int i = 0; i < (int)cars.size(); i++) {
            bool sameLane = cars[i].getPosition().lane == lane;
            bool higherUp = cars[i].getPosition().y < nearestY;
            if (sameLane && higherUp) {
                nearestY = cars[i].getPosition().y;
            }
        }
        if (nearestY > bestNearestY) {
            bestNearestY = nearestY;
            bestLane = lane;
        }
    }

    Position result;
    result.lane = bestLane;
    result.y = bestNearestY - MIN_GAP;   // MIN_GAP behind the nearest car
    if (result.y > 0) {
        result.y = 0;
    }
    return result;
}

std::vector<Car> CarManager::getCarStates() {
    carsMutex.lock();
    std::vector<Car> copy = cars;
    carsMutex.unlock();
    return copy;
}

// ---- design 1: one thread per car ----

void CarManager::start() {
    running = true;
    for (int i = 0; i < (int)cars.size(); i++) {
        carThreads.push_back(std::thread(&CarManager::oneCarLoop, this, i));
    }
}

void CarManager::stop() {
    running = false;
    for (int i = 0; i < (int)carThreads.size(); i++) {
        if (carThreads[i].joinable()) {
            carThreads[i].join();
        }
    }
}

// One per car. `cars` is never resized, so cars[index] stays valid.
void CarManager::oneCarLoop(int index) {
    while (running) {
        carsMutex.lock();

        stepCarUnlocked(cars[index]);

        if (cars[index].getPosition().y > OFFSCREEN_Y) {
            // This car left the screen: put a fresh one back at a safe spot.
            Position p = generateSafeStartPosition();
            cars[index] = Car(nextCarId);
            nextCarId++;
            cars[index].setLane(p.lane);
            cars[index].setY(p.y);
        }

        carsMutex.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(TICK_MS));
    }
}
