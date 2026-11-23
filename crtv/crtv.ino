#include <JPEGDEC.h>
#include "PanelLan.h"
#include "Config.h"
#include "MjpegPlayer.h"
#include "WaypointTracker.h"
#include "CrtvGraphics.h"
#include "CrtvAudio.h"

PanelLan tft(BOARD_SC01_PLUS);
JPEGDEC jpeg;

bool screenWasBlanked = false;

int JPEGDraw(JPEGDRAW* pDraw) {
  tft.pushImage(pDraw->x, pDraw->y, pDraw->iWidth, pDraw->iHeight, pDraw->pPixels);
  return 1;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  randomSeed(analogRead(0) ^ micros());

  // 1. Initialize GY-521 sensor
  initSensorTracker();
  initCrtvAudio();
  // 2. Initialize SD card & PSRAM frame buffers
  if (!initMjpegStorage()) {
    while (1) delay(100);
  }

  // 3. Release shared pins, initialize LCD, and allocate PSRAM sprite pool
  switchPinsToLCD();
  tft.begin();
  tft.setRotation(3);
  tft.setBrightness(255);
  tft.fillScreen(TFT_BLACK);

  initCrtvGraphics(&tft);
}

void loop() {
  resetMjpegCache();

  switchPinsToSD();
  File mjpegFile = SD.open("/output7.mjpeg", FILE_READ);
  if (!mjpegFile) {
    Serial.println("ERROR: Could not open /output7.mjpeg");
    delay(2000);
    return;
  }

  uint8_t* frameBuf = getJpegFrameBuffer();

  while (true) {
    unsigned long frameStartMs = millis();

    size_t frameBytes = readNextJpegFrame(mjpegFile, frameBuf, MAX_FRAME_SIZE);
    if (frameBytes == 0) break;

    switchPinsToLCD();

    updateWaypointTracker();
    const WaypointState& wp = getWaypointState();
    updateCrtvAudio(!wp.showArrow, wp.screenFlatBlanked, abs(wp.lockedRelYawDeg));

    // Turn off display and backlight when facing up or facing down
    if (wp.screenFlatBlanked) {
      if (!screenWasBlanked) {
        tft.fillScreen(TFT_BLACK);
        tft.setBrightness(0);
        screenWasBlanked = true;
      }
      unsigned long elapsed = millis() - frameStartMs;
      if (elapsed < FRAME_INTERVAL_MS) {
        delay(FRAME_INTERVAL_MS - elapsed);
      }
      continue;
    } else if (screenWasBlanked) {
      tft.setBrightness(255);
      screenWasBlanked = false;
    }

    if (jpeg.openRAM(frameBuf, frameBytes, JPEGDraw)) {
      jpeg.setPixelType(RGB565_BIG_ENDIAN);
      tft.startWrite();
      jpeg.decode(0, 0, 0);

      if (wp.showArrow) {
        int gx = (random(0, 100) < 20) ? random(-1, 2) : 0;
        int gy = (random(0, 100) < 20) ? random(-1, 2) : 0;
        drawPivotingOpenParen(CENTER_X + gx, CENTER_Y + gy, ORBIT_OFFSET_PX, wp.arrowAngleDeg, TFT_YELLOW);

        if (random(0, 100) < 18) {
          int glitchY = random(20, SCREEN_H - 20);
          tft.fillRect(0, glitchY, SCREEN_W, random(2, 5), 0x3000);
        }
      } else {
        drawCachedHumanSilhouette(round(wp.lockedScreenX), round(wp.lockedScreenY),
                                  wp.lockedRelYawDeg, wp.lockedRelPitchDeg, wp.targetScale);
      }

      drawCRTVBezel();

      tft.endWrite();
      jpeg.close();
    }

    unsigned long elapsed = millis() - frameStartMs;
    if (elapsed < FRAME_INTERVAL_MS) {
      delay(FRAME_INTERVAL_MS - elapsed);
    }
  }

  switchPinsToSD();
  mjpegFile.close();
}