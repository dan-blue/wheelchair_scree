#include "LidarPolar.h"
#include <math.h>

LidarPolar::LidarPolar(TFT_eSPI* tft, int x, int y, int w, int h, uint16_t c, uint16_t range)
    : Widget(tft, x, y, w, h), maxRange(range), color(c) {
    for (int i = 0; i < 360; i++) distances[i] = 0;
    cx = w / 2;
    cy = h / 2;
}

void LidarPolar::updatePoint(uint16_t angle, uint16_t distance) {
    if (angle >= 360) return;
    distances[angle] = distance;
    dirty = true;
}

void LidarPolar::draw() {
    if (!dirty) return;

    sprite->fillSprite(TFT_BLACK);
    sprite->drawCircle(cx, cy, w / 4, TFT_DARKGREY);
    sprite->drawCircle(cx, cy, (w / 2) - 1, TFT_DARKGREY);
    sprite->drawLine(cx, 0, cx, h, TFT_DARKGREY);
    sprite->drawLine(0, cy, w, cy, TFT_DARKGREY);

    for (int theta = 0; theta < 360; theta++) {
        uint16_t dist = distances[theta];
        if (dist > 0 && dist < maxRange) {
            float r_pixel = (float)dist / maxRange * (w / 2);
            float rad = theta * (PI / 180.0);
            int px = cx + (r_pixel * cos(rad - PI / 2));
            int py = cy + (r_pixel * sin(rad - PI / 2));
            sprite->drawPixel(px, py, color);
        }
    }

    sprite->setTextColor(TFT_WHITE);
    sprite->drawString("RADAR", 5, 5);
}
