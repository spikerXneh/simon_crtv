#pragma once
#include <Arduino.h>
#include "PanelLan.h"

const int POSE_COUNT = 4;

void initCrtvGraphics(PanelLan* display);
void cacheSinglePose(int pose);
void drawCachedHumanSilhouette(int cx, int cy, float relYawDeg, float relPitchDeg, float scale);
void drawPivotingOpenParen(int pivotX, int pivotY, float orbitRadius, float angleDeg, uint16_t color);
void drawCRTVBezel();
