#pragma once
#include <Arduino.h>

// Custom I2C pins on WT32-SC01 Plus Extended IO header
#define I2C_SDA 10
#define I2C_SCL 11

// Onboard SD Card Pins (multiplexed with parallel LCD)
#define SD_CS   41
#define SD_MOSI 40
#define SD_CLK  39
#define SD_MISO 38

// PSRAM Buffer Sizes
const size_t MAX_FRAME_SIZE = 65536;
const size_t SD_CACHE_SIZE  = 16384;

// Screen Dimensions (Landscape 480x320)
const unsigned long FRAME_INTERVAL_MS = 50; // 20 FPS
const int SCREEN_W = 480;
const int SCREEN_H = 320;
const int CENTER_X = SCREEN_W / 2; // 240
const int CENTER_Y = SCREEN_H / 2; // 160

// =========================================================================
// TUNING KNOBS
// =========================================================================
const float ORBIT_OFFSET_PX         = 60.0;  // Distance from center '(' pivots around
const float TARGET_RADIUS_PX        = 130.0; // Center capture zone radius
const unsigned long LOCK_DURATION_MS = 10000; // Show silhouette for 10s on lock
const float PIXELS_PER_DEG          = 5.5;   // Movement sensitivity
const float MAX_UPRIGHT_PITCH_DEG   = 55.0;  // Blank screen/backlight when facing up or down

// Random size range when locked (0.60 = distant/smaller, 1.30 = close/larger)
const float MIN_HUMAN_SCALE = 0.60;
const float MAX_HUMAN_SCALE = 1.30;

// Dark charcoal gray color for the human silhouette
const uint16_t SILHOUETTE_COLOR = 0x39E7;

// Set to -1 to randomize between all 4 realistic poses, or 0..3 for a specific pose:
// 0 = Natural Standing, 1 = Mid-Stride Walk, 2 = Ready Stance, 3 = Casual Observation
const int FIXED_POSE_MODE = -1;

// Flip between 1.0 and -1.0 if left/right or up/down moves the wrong way
const float YAW_SIGN   = -1.0;
const float PITCH_SIGN = -1.0;
