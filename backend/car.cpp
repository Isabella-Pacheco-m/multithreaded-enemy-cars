#include "car.h"
#include <cstdlib>

Car::Car(int id) {
    this->id = id;

    // A car can start in any lane, but always at the top of the screen.
    this->pos.y = 0;
    this->pos.lane = rand() % NUM_LANES;

    int variantIndex = rand() % NUM_VARIANTS;
    this->variant = static_cast<CarVariant>(variantIndex);

    this->speed = 4 + (rand() % 5);   // 4..8 px per tick
}

void Car::move() {
    this->pos.y += this->speed;
}

void Car::setLane(int lane) {
    this->pos.lane = lane;
}

void Car::setY(int y) {
    this->pos.y = y;
}

int Car::proposeLane() const {
    // Rarely (about once every 20 seconds at 10 ticks/s) try to move over
    // by ONE lane, so a car never jumps across the whole road.
    int result = this->pos.lane;

    bool tryChange = (rand() % 200 == 0);
    if (tryChange) {
        int direction = (rand() % 2 == 0) ? -1 : 1;
        int target = this->pos.lane + direction;
        if (target >= 0 && target < NUM_LANES) {
            result = target;
        }
    }
    return result;
}

int Car::getId() const {
    return id;
}

CarVariant Car::getVariant() const {
    return variant;
}

Position Car::getPosition() const {
    return pos;
}
