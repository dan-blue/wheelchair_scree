#include "Widget.h"

Widget::Widget(TFT_eSPI* tft, int16_t _x, int16_t _y, uint16_t _w, uint16_t _h)
    : x(_x), y(_y), w(_w), h(_h) {
    sprite = new TFT_eSprite(tft);
    sprite->setColorDepth(16);
    sprite->createSprite(w, h);
    dirty = true;
}

Widget::~Widget() {
    sprite->deleteSprite();
    delete sprite;
}

void Widget::push() {
    if (dirty) {
        sprite->pushSprite(x, y);
        dirty = false;
    }
}
