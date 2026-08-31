#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <string>
#include <vector>
#include "car.h"

// Build the JSON line the frontend expects:
//   {"tick":123,"design":2,"cars":[{"id":7,"lane":1,"y":320,"variant":3}]}
inline std::string serialize(const std::vector<Car>& cars, long tick, int design) {
    std::string out = "{\"tick\":" + std::to_string(tick) +
                      ",\"design\":" + std::to_string(design) + ",\"cars\":[";

    for (int i = 0; i < (int)cars.size(); i++) {
        Position p = cars[i].getPosition();
        int variant = (int)cars[i].getVariant();

        if (i > 0) {
            out += ",";
        }
        out += "{\"id\":" + std::to_string(cars[i].getId());
        out += ",\"lane\":" + std::to_string(p.lane);
        out += ",\"y\":" + std::to_string(p.y);
        out += ",\"variant\":" + std::to_string(variant) + "}";
    }

    out += "]}";
    return out;
}

#endif
