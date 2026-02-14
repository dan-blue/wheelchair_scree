#include "LidarGraph.h"

LidarGraph::LidarGraph(TFT_eSPI* tft, int x, int y, int w, int h, uint16_t c)
    : Widget(tft, x, y, w, h), color(c), maxVal(100) {
    data.reserve(w);
    for (int i = 0; i < w; i++) data.push_back(0);
}

void LidarGraph::addPoint(int val) {
    data.push_back(val);
    if (data.size() > (size_t)w) data.erase(data.begin());
    dirty = true;
}

void LidarGraph::draw() {
    if (!dirty) return;

    sprite->fillSprite(TFT_BLACK);
    sprite->drawRect(0, 0, w, h, TFT_DARKGREY);
    sprite->drawLine(0, h / 2, w, h / 2, TFT_DARKGREY);

    for (size_t i = 1; i < data.size(); i++) {
        int y1 = map(data[i - 1], 0, maxVal, h - 2, 2);
        int y2 = map(data[i], 0, maxVal, h - 2, 2);
        sprite->drawLine((int)i - 1, y1, (int)i, y2, color);
    }

    sprite->setTextColor(TFT_WHITE);
    sprite->drawString("LIDAR", 5, 5);
}
