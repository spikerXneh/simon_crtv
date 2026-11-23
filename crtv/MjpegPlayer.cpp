#include "MjpegPlayer.h"
#include "Config.h"

static SPIClass sdSPI(HSPI);
static uint8_t* jpegFrameBuf = nullptr;
static uint8_t* sdCacheBuf   = nullptr;
static size_t cachePos = 0;
static size_t cacheLen = 0;

void switchPinsToSD() {
	sdSPI.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);
}

void switchPinsToLCD() {
	sdSPI.end();
	pinMode(40, OUTPUT);
	pinMode(41, OUTPUT);
}

bool initMjpegStorage() {
	pinMode(SD_MISO, INPUT_PULLUP);
	sdSPI.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);

	if (!SD.begin(SD_CS, sdSPI, 20000000)) {
		Serial.println("ERROR: SD Card Mount Failed!");
		return false;
	}

	jpegFrameBuf = (uint8_t*)ps_malloc(MAX_FRAME_SIZE);
	sdCacheBuf   = (uint8_t*)ps_malloc(SD_CACHE_SIZE);

	if (!jpegFrameBuf || !sdCacheBuf) {
		Serial.println("ERROR: PSRAM allocation failed!");
		return false;
	}
	return true;
}

void resetMjpegCache() {
	cachePos = 0;
	cacheLen = 0;
}

uint8_t* getJpegFrameBuffer() {
	return jpegFrameBuf;
}

static int readCachedByte(File& file) {
	if (cachePos >= cacheLen) {
		switchPinsToSD();
		cacheLen = file.read(sdCacheBuf, SD_CACHE_SIZE);
		cachePos = 0;
		if (cacheLen == 0) return -1;
	}
	return sdCacheBuf[cachePos++];
}

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
