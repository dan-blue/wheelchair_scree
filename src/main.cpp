#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include "Adafruit_ST7796S.h"
#include "Agora20pt7b.h"

// -------- Pin mapping --------
#define LED 8
#define CS 1
#define RS 9 
#define RESET 10
#define MOSI 3
#define SCK 2

#define TFT_BL LED
#define TFT_CS CS
#define TFT_DC RS
#define TFT_RST RESET
#define TFT_MOSI MOSI
#define TFT_SCK SCK

#define TFT_WIDTH (480)
#define TFT_HEIGHT (320)

#define CONWAY_GRID (100)
#define SCALE (2)
#define X_OFFSET (TFT_WIDTH - (CONWAY_GRID * SCALE)) / 2
#define Y_OFFSET (TFT_HEIGHT - (CONWAY_GRID * SCALE)) / 2
// Colors (5-6-5 format)
#define C_BLACK 0x0000
#define C_WHITE 0xFFFF
#define C_BLUE  0x001F
#define C_GREEN 0x07E0

static uint8_t grid[CONWAY_GRID][CONWAY_GRID];
static uint8_t prev[CONWAY_GRID][CONWAY_GRID];

typedef enum {
    RENDER_LOGO,
    RENDER_APP    // Renamed from WAIT to imply the main app running
} main_state_t;

main_state_t c_state = RENDER_LOGO;
main_state_t n_state = RENDER_LOGO;

int8_t offsets[8][2] = { 
    {-1, -1}, {0, -1}, {1, -1},
    {-1,  0},        {1,  0},
    {-1,  1}, {0,  1}, {1,  1}
};

Adafruit_ST7796S tft = Adafruit_ST7796S(TFT_CS, TFT_DC, TFT_RST);

void setup() {
    randomSeed(micros());
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    Serial.begin(115200);
    // SPI Setup for RP2040
    SPI.setSCK(TFT_SCK);
    SPI.setTX(TFT_MOSI);
    SPI.begin();

    tft.init();
    tft.setRotation(1); // Landscape usually looks best for dashboards
    tft.fillScreen(C_WHITE);

    // allocate random grid
    for (int i = 0; i < CONWAY_GRID; i++)
    {
        for (int j = 0; j < CONWAY_GRID; j++)
        {
            grid[i][j] = (rand() % 100) < 10;
        }
    }
    memcpy(prev, grid, sizeof(grid));

}

// --- ANIMATION VARIABLES ---
unsigned long animStartTime = 0;
int currentRadius = 10;
int growDirection = 1;
uint16_t loadingProgress = 0; // 0 to 480 (width of screen)

void playStartupAnimation() {
    //tft.setFont(&Agora20pt7b);
    static unsigned long lastFrameTime = 0;
    unsigned long now = millis();
    
    // Limit to ~30 FPS (33ms) to leave CPU time for sensor init
    if (now - lastFrameTime < 33) return; 
    lastFrameTime = now;

    int centerX = tft.width() / 2;
    int centerY = tft.height() / 2;

    /*
    // 1. ERASE PREVIOUS FRAME
    // Instead of clearing the whole screen, just overwrite the old circle with Black
    tft.drawCircle(centerX, centerY, currentRadius, C_BLACK);
    tft.drawCircle(centerX, centerY, currentRadius - 1, C_BLACK); // Make lines thicker implies erasing thicker
    */
    // Simulate loading progress
    loadingProgress += 1; 

    for (int i = 0; i < CONWAY_GRID; i++) {
      for (int j = 0; j < CONWAY_GRID; j++) {
        int16_t x = X_OFFSET + j * SCALE;
        int16_t y = Y_OFFSET + i * SCALE;

        uint16_t color = grid[i][j] ? C_GREEN : C_WHITE;
        if (prev[i][j] != grid[i][j])
        {
            // SCALE==2: draw a 2x2 block so it’s visible
            tft.fillRect(x, y, SCALE, SCALE, color);
        }
      }
    }

    // copy memory to the prev
    memcpy(prev, grid, sizeof(grid));

    for (int i = 0; i < CONWAY_GRID; i++) {
      for (int j = 0; j < CONWAY_GRID; j++) {
        // loop through offsets
        uint8_t living_num = 0;
        for (int z = 0; z < 8; z++)
        {
            int ni = (i + offsets[z][0] + CONWAY_GRID) % CONWAY_GRID;
            int nj = (j + offsets[z][1] + CONWAY_GRID) % CONWAY_GRID;

            if (prev[ni][nj]) {
                living_num++;
            }
        }
        if (grid[i][j])
        {
            if (living_num < 2) grid[i][j] = 0;
            if (living_num > 3) grid[i][j] = 0;
        }
        else 
        {
            if (living_num == 3) {grid[i][j] = 1; }
        }
      }
    }
    // Simulation step

    
    // Draw Loading Bar at bottom
    // We only draw the *new* segment to save SPI bandwidth
    tft.fillRect(0, tft.height() - 10, loadingProgress, 10, C_GREEN);

    // 4. CHECK EXIT CONDITION
    if (loadingProgress >= tft.width()) {
        n_state = RENDER_APP; // Trigger state change
        
        // Cleanup: Clear screen for the main app to take over
        tft.fillScreen(C_BLACK); 
        
        // Optional: Draw static UI elements here once (Backgrounds, Headers)
        tft.setCursor(10, 10);
        tft.setTextColor(C_WHITE);
        tft.setTextSize(2);
        tft.print("LIVE DATA FEED");
    }
}

void loop() {
    // State Switch
    switch (c_state) {
        case RENDER_LOGO:
            playStartupAnimation();
            break;

        case RENDER_APP:
            tft.setFont(NULL);
            // This is where your Lidar/Map loop goes
            // For now, just a blinky heartbeat to prove we made it here
            static long last = 0;
            if (millis() - last > 500) {
                last = millis();
                Serial.println("Running Main App Loop...");
            }
            break;
    }
    
    // Commit state change
    c_state = n_state;
}