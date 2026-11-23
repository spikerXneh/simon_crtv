#include "WaypointTracker.h"
#include "Config.h"
#include "CrtvGraphics.h"
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

static Adafruit_MPU6050 mpu;
static bool mpuReady = false;

static float deviceYawDeg = 0.0;
static float devicePitchDeg = 0.0;
static float gyroOffsetX = 0.0, gyroOffsetY = 0.0, gyroOffsetZ = 0.0;
static unsigned long lastGyroReadUs = 0;

static float targetYawDeg = 0.0;
static float targetPitchDeg = 0.0;
static bool targetActive = false;
static bool targetLocked = false;
static unsigned long lockStartMs = 0;

static WaypointState state = {
	false, true, 0.0,
	(float)CENTER_X, (float)CENTER_Y,
	0.0, 0.0, 1.0, 0
};

static float normalizeAngle(float angle) {
	while (angle > 180.0) angle -= 360.0;
	while (angle < -180.0) angle += 360.0;
	return angle;
}

static void calibrateGyro() {
	if (!mpuReady) return;
	Serial.println("Hold screen upright and still for 1s calibration...");
	float sumX = 0.0, sumY = 0.0, sumZ = 0.0;
	const int samples = 100;

	for (int i = 0; i < samples; i++) {
		sensors_event_t a, g, temp;
		mpu.getEvent(&a, &g, &temp);
		sumX += g.gyro.x;
		sumY += g.gyro.y;
		sumZ += g.gyro.z;
		delay(10);
	}

	gyroOffsetX = sumX / samples;
	gyroOffsetY = sumY / samples;
	gyroOffsetZ = sumZ / samples;
	lastGyroReadUs = micros();
	Serial.println("Calibration complete.");
}

static void pickNewTarget() {
	float yawOffset = random(45, 106) * (random(0, 2) == 0 ? 1.0 : -1.0);
	targetYawDeg = normalizeAngle(deviceYawDeg + yawOffset);
	targetPitchDeg = random(-10, 11);

	state.targetScale = random((int)(MIN_HUMAN_SCALE * 100), (int)(MAX_HUMAN_SCALE * 100) + 1) / 100.0;
	state.targetPose = (FIXED_POSE_MODE >= 0 && FIXED_POSE_MODE < POSE_COUNT) ? FIXED_POSE_MODE : random(0, POSE_COUNT);

	cacheSinglePose(state.targetPose);

	targetActive = true;
	targetLocked = false;
	state.showArrow = true;
	Serial.printf("New target -> Yaw: %.1f, Pose: %d, Scale: %.2fx\n", targetYawDeg, state.targetPose, state.targetScale);
}

void initSensorTracker() {
	Wire1.begin(I2C_SDA, I2C_SCL);
	if (mpu.begin(0x68, &Wire1)) {
		mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
		mpu.setGyroRange(MPU6050_RANGE_250_DEG);
		mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
		mpuReady = true;
		calibrateGyro();
	} else {
		Serial.println("WARNING: GY-521 not found on GPIO 10/11!");
	}
}

void updateWaypointTracker() {
	if (mpuReady) {
		sensors_event_t a, g, temp;
		mpu.getEvent(&a, &g, &temp);
		// Inside updateWaypointTracker() in WaypointTracker.cpp:
		float deltaYaw = normalizeAngle(targetYawDeg - deviceYawDeg);
		float deltaPitch = targetPitchDeg - devicePitchDeg;
		state.lockedRelYawDeg = deltaYaw;  // Keeps angular distance updated for audio proximity


		unsigned long nowUs = micros();
		float dt = (nowUs - lastGyroReadUs) / 1000000.0;
		lastGyroReadUs = nowUs;

		float gx = g.gyro.x - gyroOffsetX;
		float gy = g.gyro.y - gyroOffsetY;
		float gz = g.gyro.z - gyroOffsetZ;

		float ax = a.acceleration.x;
		float ay = a.acceleration.y;
		float az = a.acceleration.z;
		float accelMag = sqrt(ax * ax + ay * ay + az * az);

		if (accelMag > 6.0 && accelMag < 14.0) {
			float verticalGyroRad = (gx * ax + gy * ay + gz * az) / accelMag;
			float yawRateDeg = verticalGyroRad * 180.0 / PI;

			if (abs(yawRateDeg) > 1.5) {
				deviceYawDeg = normalizeAngle(deviceYawDeg + YAW_SIGN * yawRateDeg * dt);
			}

			float rawPitchDeg = PITCH_SIGN * asin(constrain(-az / accelMag, -1.0, 1.0)) * 180.0 / PI;
			devicePitchDeg = (devicePitchDeg * 0.78) + (rawPitchDeg * 0.22);
		}
	}

	// Blank screen & backlight if facing up toward ceiling or down toward floor
	if (!state.screenFlatBlanked && abs(devicePitchDeg) > MAX_UPRIGHT_PITCH_DEG) {
		state.screenFlatBlanked = true;
	} else if (state.screenFlatBlanked && abs(devicePitchDeg) < (MAX_UPRIGHT_PITCH_DEG - 5.0)) {
		state.screenFlatBlanked = false;
	}

	if (!targetActive) {
		pickNewTarget();
	}

	float deltaYaw = normalizeAngle(targetYawDeg - deviceYawDeg);
	float deltaPitch = targetPitchDeg - devicePitchDeg;

	float rawDx = deltaYaw * PIXELS_PER_DEG;
	float rawDy = -deltaPitch * PIXELS_PER_DEG;

	bool behindUser = (abs(deltaYaw) > 110.0);
	float dirDx = behindUser ? ((deltaYaw > 0 ? 1.0 : -1.0) * 1000.0) : rawDx;
	float dirDy = rawDy;

	bool offScreen = behindUser || (abs(rawDx) > CENTER_X - 40) || (abs(rawDy) > CENTER_Y - 40);

	// 1. Check if 10-second lock timer has expired
	if (targetLocked && (millis() - lockStartMs >= LOCK_DURATION_MS)) {
		pickNewTarget();
		return;
	}

	// 2. If not locked yet, check if we just entered the center capture zone
	if (!targetLocked && !offScreen) {
		float distFromCenter = sqrt(rawDx * rawDx + rawDy * rawDy);
		if (distFromCenter <= TARGET_RADIUS_PX) {
			targetLocked = true;
			lockStartMs = millis();
		}
	}

	// 3. Show dark-gray human silhouette if locked & on-screen; otherwise show 60px-orbiting '('
	if (targetLocked && !offScreen) {
		state.showArrow = false;
		state.lockedScreenX = CENTER_X + rawDx;
		state.lockedScreenY = CENTER_Y + rawDy;
		state.lockedRelYawDeg = deltaYaw;
		state.lockedRelPitchDeg = deltaPitch;
		return;
	}

	state.showArrow = true;

	float desiredAngle = atan2(dirDx, -dirDy) * 180.0 / PI;
	float diff = normalizeAngle(desiredAngle - state.arrowAngleDeg);
	state.arrowAngleDeg = normalizeAngle(state.arrowAngleDeg + diff * 0.40);
}

const WaypointState& getWaypointState() {
	return state;
}
