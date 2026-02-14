#ifndef WIDGET_H
#define WIDGET_H

#include <Arduino.h>
#include <TFT_eSPI.h>

class Widget {
public:
    TFT_eSprite* sprite;
    int16_t x, y, w, h;
    bool dirty;

    Widget(TFT_eSPI* tft, int16_t _x, int16_t _y, uint16_t _w, uint16_t _h);
    virtual ~Widget();

    virtual void draw() = 0;
    void push();
};

#endif
