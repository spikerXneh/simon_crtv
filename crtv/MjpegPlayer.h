#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

void switchPinsToSD();
void switchPinsToLCD();
bool initMjpegStorage();
void resetMjpegCache();
size_t readNextJpegFrame(File& file, uint8_t* buf, size_t maxSize);
uint8_t* getJpegFrameBuffer();
