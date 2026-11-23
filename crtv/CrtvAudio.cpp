#include "CrtvAudio.h"
#include <ESP_I2S.h>

// WT32-SC01 Plus Onboard NS4168 I2S Amplifier Pins
#define I2S_BCLK 36
#define I2S_LRC  35
#define I2S_DOUT 37

const int SAMPLE_RATE = 16000;
const int AUDIO_BUF_SAMPLES = 256; // 16 ms chunk per update

static I2SClass i2s;
static int16_t audioBuf[AUDIO_BUF_SAMPLES];
static uint32_t lfsr = 0xACE1u;    // Fast white-noise generator
static float humPhase = 0.0;
static float tonePhase = 0.0;
static bool audioReady = false;

void initCrtvAudio() {
	i2s.setPins(I2S_BCLK, I2S_LRC, I2S_DOUT);
	if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
		Serial.println("WARNING: Failed to initialize I2S audio!");
		audioReady = false;
		return;
	}
	audioReady = true;
	Serial.println("I2S Audio initialized on GPIO 35/36/37!");
}

void updateCrtvAudio(bool isLocked, bool isScreenBlanked, float angularDistanceDeg) {
	if (!audioReady) return;

	// Mute completely when the screen is facing up or down
	if (isScreenBlanked) {
		memset(audioBuf, 0, sizeof(audioBuf));
		i2s.write((uint8_t*)audioBuf, sizeof(audioBuf));
		return;
	}

	// Proximity factor: 0.0 when far away, 1.0 when pointing straight at the entity
	float proximity = 1.0 - constrain(angularDistanceDeg / 120.0, 0.0, 1.0);

	// Base static volume rises as you get closer; drops into an eerie low hum when locked
	float staticAmp = isLocked ? 900.0 : (1400.0 + proximity * 1600.0);
	float toneFreq  = isLocked ? 115.0 : (180.0 + proximity * 420.0);
	float toneAmp   = isLocked ? 1400.0 : (200.0 + proximity * 900.0);

	for (int i = 0; i < AUDIO_BUF_SAMPLES; i++) {
		// 1. Fast LFSR Analog White-Noise Static
		uint32_t bit = ((lfsr >> 0) ^ (lfsr >> 2) ^ (lfsr >> 3) ^ (lfsr >> 5)) & 1;
		lfsr = (lfsr >> 1) | (bit << 15);
		float noise = ((float)(lfsr & 0xFFFF) / 32768.0 - 1.0) * staticAmp;

		// 2. 60Hz CRT Mains Hum
		humPhase += (2.0 * PI * 60.0) / SAMPLE_RATE;
		if (humPhase > 2.0 * PI) humPhase -= 2.0 * PI;
		float hum = sin(humPhase) * 450.0;

		// 3. Eerie RF Carrier / Lock Tone
		tonePhase += (2.0 * PI * toneFreq) / SAMPLE_RATE;
		if (tonePhase > 2.0 * PI) tonePhase -= 2.0 * PI;
		float carrier = sin(tonePhase) * toneAmp;

		float mixed = constrain(noise + hum + carrier, -28000.0, 28000.0);
		audioBuf[i] = (int16_t)mixed;
	}

	i2s.write((uint8_t*)audioBuf, sizeof(audioBuf));
}
