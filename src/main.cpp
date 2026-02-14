#include <SPI.h>
#include <TFT_eSPI.h> 

// -------- Configuration --------
#define CONWAY_GRID (100)
#define SCALE (2)
// Sprite size = 100 * 2 = 200 pixels square
#define SPRITE_W (CONWAY_GRID * SCALE)
#define SPRITE_H (CONWAY_GRID * SCALE)

// Center the game on the screen (assuming 480x320)
#define SCREEN_W 480
#define SCREEN_H 320
#define X_OFFSET ((SCREEN_W - SPRITE_W) / 2)
#define Y_OFFSET ((SCREEN_H - SPRITE_H) / 2)

// Colors (TFT_eSPI uses TFT_ prefix usually, but hex is same)
#define C_BLACK TFT_BLACK
#define C_WHITE TFT_WHITE
#define C_GREEN TFT_GREEN

// -------- Globals --------
static uint8_t grid[CONWAY_GRID][CONWAY_GRID];
static uint8_t prev[CONWAY_GRID][CONWAY_GRID];

// 1. INITIALIZE OBJECTS
TFT_eSPI tft = TFT_eSPI();           
TFT_eSprite sprite = TFT_eSprite(&tft); 

TFT_eSprite* lidar_graph1;
TFT_eSprite* lidar_graph2;
TFT_eSprite* lidar_graph1;

typedef enum {
    RENDER_LOGO,
    RENDER_APP
} main_state_t;

main_state_t c_state = RENDER_LOGO;
main_state_t n_state = RENDER_LOGO;

const int8_t offsets[8][2] = { 
    {-1, -1}, {0, -1}, {1, -1},
    {-1,  0},        {1,  0},
    {-1,  1}, {0,  1}, {1,  1}
};

TFT_eSprite* createGraph(uint16_t width, uint16_t height, uint16_t color)
{
    // Use 'new' to allocate the object on the Heap (it survives function exit)
    TFT_eSprite* ptr = new TFT_eSprite(&tft);

    ptr->setColorDepth(16);
    ptr->createSprite(width, height); // Allocates the RAM buffer
    ptr->fillSprite(color);
    
    return ptr; // Return the address
}

void setup() {
    Serial.begin(115200);
    //randomSeed(analogRead(0)); // floating pin
    
    // 2. HARDWARE INIT
    tft.init();
    tft.setRotation(1); // Landscape
    tft.fillScreen(C_WHITE);
    
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    sprite.setColorDepth(16); // 16-bit color
    sprite.createSprite(SPRITE_W, SPRITE_H); 
    sprite.fillSprite(C_BLACK); 

    for (int i = 0; i < CONWAY_GRID; i++) {
        for (int j = 0; j < CONWAY_GRID; j++) {
            grid[i][j] = (rand() % 100) < 7; 
            
            if(grid[i][j]) {
                sprite.fillRect(j * SCALE, i * SCALE, SCALE, SCALE, C_GREEN);
            }
        }
    }
    memcpy(prev, grid, sizeof(grid));
    
    sprite.pushSprite(X_OFFSET, Y_OFFSET);

    lidar_graph1 = createGraph(200, 200); 
}

uint16_t loadingProgress = 0; 

void playStartupAnimation() {
    static unsigned long lastFrameTime = 0;
    unsigned long now = millis();
    
    if (now - lastFrameTime < 33) return; // ~30 FPS
    lastFrameTime = now;

    for (int i = 0; i < CONWAY_GRID; i++) {
      for (int j = 0; j < CONWAY_GRID; j++) {
        if (prev[i][j] != grid[i][j]) {
            uint16_t color = grid[i][j] ? C_GREEN : C_BLACK;
            
            sprite.fillRect(j * SCALE, i * SCALE, SCALE, SCALE, color);
        }
      }
    }

    sprite.pushSprite(X_OFFSET, Y_OFFSET);

    loadingProgress += 2; 
    tft.fillRect(0, tft.height() - 10, loadingProgress, 10, C_GREEN);

    memcpy(prev, grid, sizeof(grid));

    for (int i = 0; i < CONWAY_GRID; i++) {
      for (int j = 0; j < CONWAY_GRID; j++) {
        uint8_t living_num = 0;
        for (int z = 0; z < 8; z++) {
            int ni = (i + offsets[z][0] + CONWAY_GRID) % CONWAY_GRID;
            int nj = (j + offsets[z][1] + CONWAY_GRID) % CONWAY_GRID;
            if (prev[ni][nj]) living_num++;
        }

        if (prev[i][j]) { 
            if (living_num < 2 || living_num > 3) grid[i][j] = 0;
            else grid[i][j] = 1;
        } else {
            if (living_num == 3) grid[i][j] = 1;
            else grid[i][j] = 0; 
        }
      }
    }

    if (loadingProgress >= tft.width()) {
        n_state = RENDER_APP;
        
        sprite.deleteSprite(); // Free up the 80KB RAM for the main app!
        tft.fillScreen(C_BLACK); 
        
        tft.setTextDatum(MC_DATUM); // Middle Center alignment
        tft.setTextColor(C_WHITE, C_BLACK);
        tft.drawString("LIVE DATA FEED", tft.width()/2, tft.height()/2, 4); 
    }
}

void loop() {
    switch (c_state) {
        case RENDER_LOGO:
            playStartupAnimation();
            break;

        case RENDER_APP:
            static long last = 0;
            if (millis() - last > 500) {
                lidar_graph1->pushSprite(100, 100);
                last = millis();
            }
            break;
    }
    c_state = n_state;
}