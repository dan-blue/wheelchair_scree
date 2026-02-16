#include <SPI.h>
#include <TFT_eSPI.h> 
#include <math.h>
#include <cstdint>
#include <stddef.h>
#include <stdio.h>
#include "LidarPolar.h"
#include "LidarGraph.h"
#include "ProxBar.h"
#include "ogoa.h"


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
ogoa_ctx_t ogoa_link;
static uint32_t lastLidarUpdateMs = 0;
static uint32_t rxAckCount = 0;
static uint32_t rxStatusReqCount = 0;
static uint32_t rxStatusRespCount = 0;
static uint32_t rxLidarCount = 0;
static uint32_t rxUnknownCount = 0;
static uint8_t remoteMode = 0;
static uint8_t remoteX = 0;
static uint8_t remoteY = 0;
static uint32_t lastStatusRespMs = 0;
static char lastProtoEvent[64] = "OGOA init";
static uint32_t lastProtoEventMs = 0;

static int ogoaSerialTx(void *user_ctx, const uint8_t *data, size_t len) {
    Stream *serial = static_cast<Stream *>(user_ctx);
    if (serial == nullptr || data == nullptr) {
        return 0;
    }
    return (int)serial->write(data, len);
}

static void sendLocalStatusFrame() {
    uint8_t payload[3];
    payload[0] = 0u;
    payload[1] = remoteX;
    payload[2] = remoteY;
    (void)ogoa_send(&ogoa_link, OGOA_TYPE_STATUS_RESPONSE, payload, (uint8_t)sizeof(payload), millis());
}

static void drawProtocolOverlay() {
    char l1[64];
    char l2[96];
    char l3[128];
    char l4[128];
    size_t pos = 0u;
    uint32_t ageMs = millis() - lastProtoEventMs;
    uint32_t txAgeMs = millis() - ogoa_link.tx_last_action_ms;

    snprintf(l1, sizeof(l1), "%s (%lums)", lastProtoEvent, (unsigned long)ageMs);
    snprintf(
        l2,
        sizeof(l2),
        "ack:%lu req:%lu resp:%lu lidar:%lu unk:%lu",
        (unsigned long)rxAckCount,
        (unsigned long)rxStatusReqCount,
        (unsigned long)rxStatusRespCount,
        (unsigned long)rxLidarCount,
        (unsigned long)rxUnknownCount
    );

    pos += (size_t)snprintf(l3 + pos, sizeof(l3) - pos, "rx idx:%u st:%u ", ogoa_link.rx_index, ogoa_link.rx_state);
    for (uint8_t i = 0; i < 10u && pos < sizeof(l3); ++i) {
        pos += (size_t)snprintf(l3 + pos, sizeof(l3) - pos, "%02X ", ogoa_link.rx_buf[i]);
    }

    snprintf(
        l4,
        sizeof(l4),
        "tx wait:%u pend:%u retry:%u loop:%u age:%lums m:%u x:%u y:%u",
        ogoa_link.tx_waiting_ack,
        ogoa_link.tx_pending_seq,
        ogoa_link.tx_retried_once,
        ogoa_link.tx_status_loop,
        (unsigned long)txAgeMs,
        remoteMode,
        remoteX,
        remoteY
    );

    tft.fillRect(0, 0, SCREEN_W, 56, C_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_WHITE, C_BLACK);
    tft.drawString(l1, 4, 2, 2);
    tft.drawString(l2, 4, 16, 1);
    tft.drawString(l3, 4, 28, 1);
    tft.drawString(l4, 4, 40, 1);
}

static void applyLidarPayload(const ogoa_frame_t *frame) {
    if (frame == nullptr || frame->len < 4u) {
        return;
    }

    uint8_t startTheta = frame->payload[0];
    uint8_t deltaTheta = frame->payload[1];
    if (deltaTheta == 0u) {
        return;
    }

    uint8_t distanceBytes = (uint8_t)(frame->len - 2u);
    uint8_t pointCount = (uint8_t)(distanceBytes / 2u);

    for (uint8_t i = 0; i < pointCount; ++i) {
        uint8_t lo = frame->payload[(size_t)2u + (size_t)i * 2u];
        uint8_t hi = frame->payload[(size_t)3u + (size_t)i * 2u];
        uint16_t distMm = (uint16_t)((uint16_t)hi << 8u) | (uint16_t)lo;
        uint16_t angle = (uint16_t)((startTheta + (uint16_t)i * deltaTheta) % 360u);

        if (frontLidar != nullptr) {
            frontLidar->updatePoint(angle, distMm);
        }
    }

    lastLidarUpdateMs = millis();
}

static void ogoaOnFrame(void *user_ctx, const ogoa_frame_t *frame) {
    (void)user_ctx;
    if (frame == nullptr) {
        return;
    }

    switch (frame->type) {
        case OGOA_TYPE_ACK:
            rxAckCount++;
            snprintf(lastProtoEvent, sizeof(lastProtoEvent), "RX ACK seq=%u", frame->seq);
            lastProtoEventMs = millis();
            break;

        case OGOA_TYPE_STATUS_REQUEST:
            rxStatusReqCount++;
            sendLocalStatusFrame();
            snprintf(lastProtoEvent, sizeof(lastProtoEvent), "RX STATUS_REQ -> TX STATUS_RESP");
            lastProtoEventMs = millis();
            break;

        case OGOA_TYPE_STATUS_RESPONSE:
            rxStatusRespCount++;
            if (frame->len >= 3u) {
                remoteMode = frame->payload[0];
                remoteX = frame->payload[1];
                remoteY = frame->payload[2];
                lastStatusRespMs = millis();
                if (proxLeft != nullptr) {
                    proxLeft->setValue((int)remoteX);
                }
                if (proxRight != nullptr) {
                    proxRight->setValue((int)remoteY);
                }
                snprintf(lastProtoEvent, sizeof(lastProtoEvent), "RX STATUS_RESP m=%u x=%u y=%u", remoteMode, remoteX, remoteY);
                lastProtoEventMs = millis();
            }
            break;

        case OGOA_TYPE_LIDAR_SEND:
            rxLidarCount++;
            applyLidarPayload(frame);
            snprintf(lastProtoEvent, sizeof(lastProtoEvent), "RX LIDAR pts=%u", (unsigned)((frame->len >= 2u) ? ((frame->len - 2u) / 2u) : 0u));
            lastProtoEventMs = millis();
            break;

        default:
            rxUnknownCount++;
            snprintf(lastProtoEvent, sizeof(lastProtoEvent), "RX UNKNOWN type=0x%02X", frame->type);
            lastProtoEventMs = millis();
            break;
    }
}

static void ogoaOnError(void *user_ctx, ogoa_err_t err) {
    Stream *serial = static_cast<Stream *>(user_ctx);
    if (serial != nullptr) {
        serial->print("OGOA ERR: ");
        serial->println((int)err);
    }
    snprintf(lastProtoEvent, sizeof(lastProtoEvent), "OGOA ERR %d", (int)err);
    lastProtoEventMs = millis();
}

ogoa_ops_t ogoa_link_ops = {
    .tx = ogoaSerialTx,
    .on_frame = ogoaOnFrame,
    .on_error = ogoaOnError
};
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
    ogoa_init(&ogoa_link, &ogoa_link_ops, static_cast<Stream *>(&Serial));
    
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
        lastProtoEventMs = millis();

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
                ogoa_tick(&ogoa_link, millis());

                // Create some noisy sine waves
                float t = millis() / 500.0;
                int val1 = 50 + 40 * sin(t); 
                int val2 = 50 + 40 * cos(t * 1.5);
                
                while (Serial.available() > 0)
                {
                    ogoa_process_byte(&ogoa_link, (uint8_t)Serial.read(), millis());
                }
                if ((millis() - lastStatusRespMs) > 750u) {
                    proxLeft->setValue(abs(val1));
                    proxRight->setValue(abs(val2));
                }

                // RENDER THE WIDGETS
                frontLidar->draw();
                frontLidar->push();

                rearLidar->draw();
                rearLidar->push();

                proxLeft->draw();
                proxLeft->push();

                proxRight->draw();
                proxRight->push();

                drawProtocolOverlay();
            }
            break;
    }
};
