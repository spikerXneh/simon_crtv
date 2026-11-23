#pragma once
#include <Arduino.h>

struct WaypointState {
	bool screenFlatBlanked;
	bool showArrow;
	float arrowAngleDeg;
	float lockedScreenX;
	float lockedScreenY;
	float lockedRelYawDeg;
	float lockedRelPitchDeg;
	float targetScale;
	int targetPose;
};

void initSensorTracker();
void updateWaypointTracker();
const WaypointState& getWaypointState();
