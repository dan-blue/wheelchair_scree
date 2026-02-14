#ifndef LIDAR_GRAPH_H
#define LIDAR_GRAPH_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <vector>
#include "Widget.h"

class LidarGraph : public Widget {
private:
    std::vector<int> data;
    uint16_t color;
    uint16_t maxVal;

public:
    LidarGraph(TFT_eSPI* tft, int x, int y, int w, int h, uint16_t c);
    void addPoint(int val);
    void draw() override;
};

#endif
