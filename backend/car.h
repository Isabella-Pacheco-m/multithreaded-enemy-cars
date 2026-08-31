#ifndef CAR_H
#define CAR_H

// Lanes on the road (matches GameConfig.LANES on the frontend).
const int NUM_LANES = 3;
// Number of car colors (design 3 uses one thread per color).
const int NUM_VARIANTS = 5;

enum class CarVariant {
    BlueStrip = 0,
    GreenStrip = 1,
    PinkStrip = 2,
    RedStrip = 3,
    WhiteStrip = 4
};

struct Position {
    int lane;
    int y;
};

// One enemy car: just data and movement rules, no threads.
class Car {
public:
    Car(int id);

    int getId() const;
    CarVariant getVariant() const;
    Position getPosition() const;

    void setLane(int lane);
    void setY(int y);
    int proposeLane() const;   // usually returns the current lane, sometimes another
    void move();               // move down by speed

private:
    int id;
    CarVariant variant;
    Position pos;
    int speed;
};

#endif
