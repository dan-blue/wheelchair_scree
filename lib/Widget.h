#ifndef WIDGET_H // Include Guard (prevents errors if included twice)
#define WIDGET_H

#include <TFT_eSPI.h>
#include <vector>

class Widget {
public:
    TFT_eSprite* sprite;
    int16_t x, y, w, h;
    bool dirty;

    Widget(TFT_eSPI* tft, int16_t _x, int16_t _y, uint16_t _w, uint16_t _h) 
        : x(_x), y(_y), w(_w), h(_h) {
        sprite = new TFT_eSprite(tft);
        sprite->setColorDepth(16);
        sprite->createSprite(w, h);
        dirty = true;
    }
    
    // Virtual Destructor is CRITICAL when using polymorphism
    virtual ~Widget() { 
        sprite->deleteSprite(); 
        delete sprite; 
    }
    
    // The "Contract"
    virtual void draw() = 0; 
    
    void push() {
        if (dirty) {
            sprite->pushSprite(x, y);
            dirty = false;
        }
    }
};

class LidarGraph : public Widget {
public:
    // Circular buffer for generic data
    std::vector<int> data; 
    uint16_t color;

    LidarGraph(TFT_eSPI* tft, int x, int y, int w, int h, uint16_t line_color) 
        : Widget(tft, x, y, w, h), color(line_color) {
        data.reserve(w); // Reserve memory for full width
    }

    void addPoint(int val) {
        data.push_back(val);
        if (data.size() > w) data.erase(data.begin()); // Scroll
        dirty = true; // Mark for redraw
    }

    void draw() override {
        if (!dirty) return;
        sprite->fillSprite(TFT_BLACK); // Clear background
        
        // Draw Border
        sprite->drawRect(0,0, w, h, TFT_DARKGREY);

        // Plot Data
        for (size_t i = 1; i < data.size(); i++) {
             // Simple map function (val 0-100 scales to height)
             int y1 = map(data[i-1], 0, 100, h-2, 2); 
             int y2 = map(data[i],   0, 100, h-2, 2);
             sprite->drawLine(i-1, y1, i, y2, color);
        }
    }
};
#endif