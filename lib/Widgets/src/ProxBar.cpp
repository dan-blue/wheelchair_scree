#include "ProxBar.h"

ProxBar::ProxBar(TFT_eSPI* tft, int x, int y, int w, int h)
    : Widget(tft, x, y, w, h) {}

void ProxBar::setValue(int v) {
    if (value != v) {
        value = v;
        dirty = true;
    }
}

void ProxBar::draw() {
    if (!dirty) return;

    uint16_t barColor = TFT_GREEN;
    if (value > 50) barColor = TFT_YELLOW;
    if (value > 80) barColor = TFT_RED;

    sprite->fillSprite(TFT_BLACK);
    int barH = map(value, 0, 100, 0, h);
    sprite->fillRect(0, h - barH, w, barH, barColor);
    sprite->drawRect(0, 0, w, h, TFT_WHITE);
}
