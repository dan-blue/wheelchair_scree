#ifndef LIDAR_POLAR_H
#define LIDAR_POLAR_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "Widget.h"

class LidarPolar : public Widget {
private:
    uint16_t distances[360];
    uint16_t maxRange;
    uint16_t color;
    int cx, cy;

public:
    LidarPolar(TFT_eSPI* tft, int x, int y, int w, int h, uint16_t c, uint16_t range);
    void updatePoint(uint16_t angle, uint16_t distance);
    void draw() override;
};

#endif
