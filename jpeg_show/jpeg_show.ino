#include <SPI.h>
#include <SD.h>
#include <JPEGDEC.h>
#include "PanelLan.h"

PanelLan tft(BOARD_SC01_PLUS);
JPEGDEC jpeg;

// Onboard SD Card Pins
#define SD_CS 41
#define SD_MOSI 40
#define SD_CLK 39
#define SD_MISO 38

SPIClass sdSPI(HSPI);

// Small buffers in PSRAM (64 KB for 1 compressed frame + 16 KB SD read cache)
const size_t MAX_FRAME_SIZE = 65536;
const size_t SD_CACHE_SIZE = 16384;

uint8_t* jpegFrameBuf = nullptr;
uint8_t* sdCacheBuf = nullptr;
size_t cachePos = 0;
size_t cacheLen = 0;

// Target frame pacing (20 FPS = 50 ms per frame)
const unsigned long FRAME_INTERVAL_MS = 50;
float arrowAngleDeg = 0.0;  // Current rotation angle in degrees

// --- Pin Multiplexing Helpers ---
void switchPinsToSD() {
	// Re-attach GPIO 40 (MOSI) and GPIO 41 (CS) to the HSPI bus
	sdSPI.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);
}

void switchPinsToLCD() {
	// Detach SPI matrix from GPIO 40 & 41 and return them to parallel LCD output mode
	sdSPI.end();
	pinMode(40, OUTPUT);
	pinMode(41, OUTPUT);
}

// --- JPEGDEC Draw Callback ---
int JPEGDraw(JPEGDRAW* pDraw) {
	tft.pushImage(pDraw->x, pDraw->y, pDraw->iWidth, pDraw->iHeight, pDraw->pPixels);
	return 1;
}

// --- Fast Cached SD Reader ---
int readCachedByte(File& file) {
	if (cachePos >= cacheLen) {
		switchPinsToSD();
		cacheLen = file.read(sdCacheBuf, SD_CACHE_SIZE);
		cachePos = 0;
		if (cacheLen == 0) return -1;  // End of file
	}
	return sdCacheBuf[cachePos++];
}

// --- Extract One Complete JPEG Frame (0xFFD8 ... 0xFFD9) ---
size_t readNextJpegFrame(File& file, uint8_t* buf, size_t maxSize) {
	size_t len = 0;
	bool foundStart = false;
	int prevByte = -1;

	while (true) {
		int currByte = readCachedByte(file);
		if (currByte < 0) break;

		if (!foundStart) {
			if (prevByte == 0xFF && currByte == 0xD8) {
				foundStart = true;
				buf[0] = 0xFF;
				buf[1] = 0xD8;
				len = 2;
			}
		} else {
			if (len >= maxSize) {
				Serial.println("Frame exceeded 64 KB buffer!");
				return 0;
			}
			buf[len++] = (uint8_t)currByte;

			if (prevByte == 0xFF && currByte == 0xD9) {
				return len;
			}
		}
		prevByte = currByte;
	}
	return 0;
}

void setup() {
	Serial.begin(115200);
	delay(1000);

	// 1. Initialize SD card first
	pinMode(SD_MISO, INPUT_PULLUP);
	sdSPI.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);

	if (!SD.begin(SD_CS, sdSPI, 20000000)) {
		Serial.println("ERROR: SD Card Mount Failed!");
		while (1) delay(100);
	}

	// 2. Allocate small streaming buffers in PSRAM
	jpegFrameBuf = (uint8_t*)ps_malloc(MAX_FRAME_SIZE);
	sdCacheBuf = (uint8_t*)ps_malloc(SD_CACHE_SIZE);

	if (!jpegFrameBuf || !sdCacheBuf) {
		Serial.println("ERROR: PSRAM allocation failed!");
		while (1) delay(100);
	}

	// 3. Release shared pins and initialize LCD
	switchPinsToLCD();
	tft.begin();
	tft.setRotation(1);
	tft.fillScreen(TFT_BLACK);
}

void loop() {
	cachePos = 0;
	cacheLen = 0;

	switchPinsToSD();
	File mjpegFile = SD.open("/output5.mjpeg", FILE_READ);
	if (!mjpegFile) {
		Serial.println("ERROR: Could not open /output5.mjpeg");
		delay(2000);
		return;
	}

	while (true) {
		unsigned long frameStartMs = millis();

		// Read next compressed frame from SD cache
		size_t frameBytes = readNextJpegFrame(mjpegFile, jpegFrameBuf, MAX_FRAME_SIZE);
		if (frameBytes == 0) break;

		// Hand GPIO 40 & 41 back to the parallel LCD before drawing
		switchPinsToLCD();

		if (Serial.available() > 0) {
			String input = Serial.readStringUntil('\n');
			input.trim();
			if (input.length() > 0) {
				arrowAngleDeg = input.toFloat();
				Serial.printf("Arrow angle set to: %.1f deg\n", arrowAngleDeg);
			}
		}

		if (jpeg.openRAM(jpegFrameBuf, frameBytes, JPEGDraw)) {
			jpeg.setPixelType(RGB565_BIG_ENDIAN);  // Change to RGB565_LITTLE_ENDIAN if colors swap
			tft.startWrite();
			jpeg.decode(0, 0, 0);

			// // 2. Draw shapes/UI directly on top of the video frame
			// tft.fillRoundRect(15, 260, 210, 45, 8, TFT_NAVY);      // Filled rounded box
			// tft.drawRoundRect(15, 260, 210, 45, 8, TFT_WHITE);     // White border
			// tft.drawCircle(40, 282, 12, TFT_RED);                  // Circle icon

			// tft.setTextColor(TFT_WHITE);
			// tft.setTextSize(2);
			// tft.drawString("DECODING", 65, 275);                    // Overlay text

			drawRotatedRightAngle(120, 160, arrowAngleDeg, TFT_YELLOW);

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
// Helper to rotate a local (lx, ly) point around (cx, cy) by angleDeg
void rotatePoint(int cx, int cy, float lx, float ly, float cosA, float sinA, int& outX, int& outY) {
	outX = cx + round(lx * cosA - ly * sinA);
	outY = cy + round(lx * sinA + ly * cosA);
}

// Helper to draw a solid quadrilateral from four rotated corner points
void fillQuad(int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3, uint16_t color) {
	tft.fillTriangle(x0, y0, x1, y1, x2, y2, color);
	tft.fillTriangle(x0, y0, x2, y2, x3, y3, color);
}

// Draws a 20px-thick right-angle chevron (90° corner) pointing toward angleDeg (0° = UP)
void drawRotatedRightAngle(int cx, int cy, float angleDeg, uint16_t color) {
	float rad = angleDeg * PI / 180.0;
	float cosA = cos(rad);
	float sinA = sin(rad);

	// Dimensions (in pixels) for an exact 90-degree corner with 20px thick arms
	const float thick = 20.0;
	const float armLen = 60.0;          // Length of each arm from the outer tip
	const float invSqrt2 = 0.70710678;  // cos(45°) = sin(45°)
	const float d = armLen * invSqrt2;  // 42.4 px projection along X and Y
	const float w = thick * invSqrt2;   // 14.1 px perpendicular offset for 20px thickness
	const float tipOffset = -20.0;      // Shifts visual center nicely over (cx, cy)

	// Outer tip (points UP at 0°) and inner 90° vertex
	float outerTipX = 0.0, outerTipY = tipOffset;
	float innerTipX = 0.0, innerTipY = tipOffset + thick * 1.41421356;  // 28.3 px below outer tip

	// Left arm outer & inner end corners
	float leftOuterX = -d, leftOuterY = tipOffset + d;
	float leftInnerX = -d + w, leftInnerY = tipOffset + d + w;

	// Right arm outer & inner end corners
	float rightOuterX = d, rightOuterY = tipOffset + d;
	float rightInnerX = d - w, rightInnerY = tipOffset + d + w;

	// Rotate all 6 points around (cx, cy)
	int oTip_x, oTip_y, iTip_x, iTip_y;
	int lOut_x, lOut_y, lIn_x, lIn_y;
	int rOut_x, rOut_y, rIn_x, rIn_y;

	rotatePoint(cx, cy, outerTipX, outerTipY, cosA, sinA, oTip_x, oTip_y);
	rotatePoint(cx, cy, innerTipX, innerTipY, cosA, sinA, iTip_x, iTip_y);
	rotatePoint(cx, cy, leftOuterX, leftOuterY, cosA, sinA, lOut_x, lOut_y);
	rotatePoint(cx, cy, leftInnerX, leftInnerY, cosA, sinA, lIn_x, lIn_y);
	rotatePoint(cx, cy, rightOuterX, rightOuterY, cosA, sinA, rOut_x, rOut_y);
	rotatePoint(cx, cy, rightInnerX, rightInnerY, cosA, sinA, rIn_x, rIn_y);

	// Draw Left Arm and Right Arm meeting cleanly at the 90° tip
	fillQuad(oTip_x, oTip_y, lOut_x, lOut_y, lIn_x, lIn_y, iTip_x, iTip_y, color);
	fillQuad(oTip_x, oTip_y, rOut_x, rOut_y, rIn_x, rIn_y, iTip_x, iTip_y, color);
}