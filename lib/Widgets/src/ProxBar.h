#ifndef PROX_BAR_H
#define PROX_BAR_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "Widget.h"

class ProxBar : public Widget {
private:
    int value = 0;

public:
    ProxBar(TFT_eSPI* tft, int x, int y, int w, int h);
    void setValue(int v);
    void draw() override;
};

#endif
