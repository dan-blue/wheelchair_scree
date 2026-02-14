#include <SPI.h>
#include <TFT_eSPI.h> 
#include <math.h>
#include <cstdint>
#include "LidarPolar.h"
#include "LidarGraph.h"
#include "ProxBar.h"


// ================= CONFIGURATION =================
#define CONWAY_GRID (100)
#define SCALE (2)
#define SPRITE_W (CONWAY_GRID * SCALE)
#define SPRITE_H (CONWAY_GRID * SCALE)

// Screen Dimensions (Landscape)
#define SCREEN_W 480
#define SCREEN_H 320

// Conway Offsets
#define X_OFFSET ((SCREEN_W - SPRITE_W) / 2)
#define Y_OFFSET ((SCREEN_H - SPRITE_H) / 2)

// Colors
#define C_BLACK TFT_BLACK
#define C_WHITE TFT_WHITE
#define C_GREEN TFT_GREEN
#define C_RED   TFT_RED
#define C_CYAN  TFT_CYAN

// ================= GLOBALS =================

TFT_eSPI tft = TFT_eSPI();           

// Intro Animation Sprite
TFT_eSprite* introSprite = nullptr; 

// Dashboard Widgets
LidarPolar* frontLidar = nullptr;
LidarPolar* rearLidar  = nullptr;
ProxBar* proxLeft   = nullptr;
ProxBar* proxRight  = nullptr;

// State Machine
typedef enum { RENDER_LOGO, RENDER_APP } main_state_t;
main_state_t c_state = RENDER_LOGO;

// Conway Globals
static uint8_t grid[CONWAY_GRID][CONWAY_GRID];
static uint8_t prev[CONWAY_GRID][CONWAY_GRID];
uint16_t loadingProgress = 0; 
const int8_t offsets[8][2] = {{-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1}};


// ================= SETUP =================
void setup() {
    Serial.begin(115200);
    
    // Hardware Init
    tft.init();
    tft.setRotation(1); 
    tft.fillScreen(C_WHITE);
    
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH); 
   
    // Initialize Intro Sprite ONLY (Save RAM)
    introSprite = new TFT_eSprite(&tft);
    introSprite->setColorDepth(16);
    introSprite->createSprite(SPRITE_W, SPRITE_H);
    introSprite->fillSprite(C_BLACK);

    // Init Conway Grid
    for (int i = 0; i < CONWAY_GRID; i++) {
        for (int j = 0; j < CONWAY_GRID; j++) {
            grid[i][j] = (rand() % 100) < 15;
            if(grid[i][j]) introSprite->fillRect(j * SCALE, i * SCALE, SCALE, SCALE, C_GREEN);
        }
    }
    memcpy(prev, grid, sizeof(grid));
    introSprite->pushSprite(X_OFFSET, Y_OFFSET);
}


// ================= ANIMATION LOOP =================
void playStartupAnimation() {
    static unsigned long lastFrameTime = 0;
    if (millis() - lastFrameTime < 33) return; 
    lastFrameTime = millis();

    // 1. Update Game of Life
    for (int i = 0; i < CONWAY_GRID; i++) {
      for (int j = 0; j < CONWAY_GRID; j++) {
        if (prev[i][j] != grid[i][j]) {
            uint16_t color = grid[i][j] ? C_GREEN : C_BLACK;
            introSprite->fillRect(j * SCALE, i * SCALE, SCALE, SCALE, color);
        }
      }
    }
    introSprite->pushSprite(X_OFFSET, Y_OFFSET);

    // 2. Loading Bar
    loadingProgress += 4; 
    tft.fillRect(0, tft.height() - 10, loadingProgress, 10, C_GREEN);

    // 3. Sim Logic
    memcpy(prev, grid, sizeof(grid));
    for (int i = 0; i < CONWAY_GRID; i++) {
      for (int j = 0; j < CONWAY_GRID; j++) {
        uint8_t living_num = 0;
        for (int z = 0; z < 8; z++) {
            int ni = (i + offsets[z][0] + CONWAY_GRID) % CONWAY_GRID;
            int nj = (j + offsets[z][1] + CONWAY_GRID) % CONWAY_GRID;
            if (prev[ni][nj]) living_num++;
        }
        if (prev[i][j]) grid[i][j] = (living_num == 2 || living_num == 3);
        else grid[i][j] = (living_num == 3);
      }
    }

    // 4. TRANSITION TO APP
    if (loadingProgress >= tft.width()) {
        
        // A. DELETE INTRO MEMORY
        introSprite->deleteSprite();
        delete introSprite;
        introSprite = nullptr;

        // B. CLEAR SCREEN
        tft.fillScreen(C_BLACK);
        
        // C. INITIALIZE WIDGETS (Now we allocate the main app memory)
        // Two big graphs in the middle
        frontLidar = new LidarPolar(&tft, 40, 50, 200, 200, C_GREEN, 4000);
        rearLidar  = new LidarPolar(&tft, 260, 50, 200, 200, C_CYAN, 4000);
        
        // Prox bars on the sides
        proxLeft   = new ProxBar(&tft, 10, 50, 20, 150);
        proxRight  = new ProxBar(&tft, 470, 50, 20, 150); // Edge of screen

        // Draw Static UI Text
        tft.setTextColor(C_WHITE, C_BLACK);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("SYSTEM READY", SCREEN_W/2, 20, 4);

        // Switch State
        c_state = RENDER_APP;
    }
}


// ================= MAIN LOOP =================
void loop() {
    switch (c_state) {
        case RENDER_LOGO:
            playStartupAnimation();
            break;

        case RENDER_APP:
            // 1. SIMULATE DATA (Replace this with real sensor reads)
            static long lastUpdate = 0;
            if (millis() - lastUpdate > 33) { // 30 FPS Update
                lastUpdate = millis();

                // Create some noisy sine waves
                float t = millis() / 500.0;
                int val1 = 50 + 40 * sin(t); 
                int val2 = 50 + 40 * cos(t * 1.5);
                
                // FEED THE WIDGETS
                for (int i = 0; i < 360; i++) {
                    uint16_t fakeDist = 0;
                    
                    // Create a fake wall at 2 meters (2000mm)
                    if (i > 60 && i < 120) fakeDist = 2000; 
                    // Random noise elsewhere
                    else if (rand() % 100 < 5) fakeDist = 3500; 

                    frontLidar->updatePoint(i, fakeDist);
                }
                
                proxLeft->setValue(abs(val1));
                proxRight->setValue(abs(val2));

                // RENDER THE WIDGETS
                frontLidar->draw();
                frontLidar->push();

                rearLidar->draw();
                rearLidar->push();

                proxLeft->draw();
                proxLeft->push();

                proxRight->draw();
                proxRight->push();
            }
            break;
    }
};
