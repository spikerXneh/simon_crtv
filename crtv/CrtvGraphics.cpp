#include "CrtvGraphics.h"
#include "Config.h"

static PanelLan* tftPtr = nullptr;

const int HUMAN_SPRITE_W = 140;
const int HUMAN_SPRITE_H = 210;
const int YAW_STEPS = 9;              // -80 to +80 deg in 20-deg steps
const int PITCH_STEPS = 3;            // -30, 0, +30 deg
const uint16_t TRANS_COLOR = 0x0001;  // 16-bit transparent key

static LGFX_Sprite* humanSprites[YAW_STEPS][PITCH_STEPS];
static int cachedPoseIndex = -1;

static void fillQuadTarget(LovyanGFX* target, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, uint16_t color) {
	target->fillTriangle(round(x0), round(y0), round(x1), round(y1), round(x2), round(y2), color);
	target->fillTriangle(round(x0), round(y0), round(x2), round(y2), round(x3), round(y3), color);
}

static void drawTaperedLimb(LovyanGFX* target, float x0, float y0, float x1, float y1, float r0, float r1, uint16_t color) {
	float dx = x1 - x0;
	float dy = y1 - y0;
	float len = sqrt(dx * dx + dy * dy);
	if (len < 0.5) {
		target->fillCircle(round(x0), round(y0), max(1, (int)round(r0)), color);
		return;
	}
	float nx = -dy / len;
	float ny = dx / len;

	target->fillCircle(round(x0), round(y0), max(1, (int)round(r0)), color);
	target->fillCircle(round(x1), round(y1), max(1, (int)round(r1)), color);

	fillQuadTarget(target,
	               x0 + nx * r0, y0 + ny * r0,
	               x0 - nx * r0, y0 - ny * r0,
	               x1 - nx * r1, y1 - ny * r1,
	               x1 + nx * r1, y1 + ny * r1, color);
}

static void renderHumanPoseToSprite(LGFX_Sprite* spr, int pose, float relYawDeg, float relPitchDeg, uint16_t color) {
	spr->fillSprite(TRANS_COLOR);

	const float H = 24.0;
	float yawRad = constrain(relYawDeg * 1.55, -82.0, 82.0) * PI / 180.0;
	float pitchRad = constrain(relPitchDeg * 1.55, -60.0, 60.0) * PI / 180.0;

	float wScale = 0.32 + 0.68 * abs(cos(yawRad));
	float hScale = 0.65 + 0.35 * abs(cos(pitchRad));
	float depthShift = sin(yawRad);
	float profileAmount = abs(depthShift);

	float cx = HUMAN_SPRITE_W * 0.5;
	float topY = 14.0 + (1.0 - hScale) * 28.0;

	// 1. Cranium, Side-Profile Face/Occiput & Neck
	float headH = H * hScale;
	float headW = (H * 0.66) * (0.58 + 0.42 * wScale);
	float headCenterX = cx + depthShift * 2.5;
	float headCenterY = topY + headH * 0.43;

	spr->fillEllipse(round(headCenterX), round(headCenterY),
	                 max(4, (int)round(headW * 0.50)),
	                 max(5, (int)round(headH * 0.43)), color);

	if (profileAmount > 0.25) {
		float occiputX = headCenterX - depthShift * (headW * 0.28);
		spr->fillCircle(round(occiputX), round(headCenterY - headH * 0.04),
		                max(3, (int)round(headH * 0.34)), color);

		float faceX = headCenterX + depthShift * (headW * 0.48);
		spr->fillTriangle(round(headCenterX + depthShift * headW * 0.2), round(headCenterY - headH * 0.08),
		                  round(faceX + depthShift * 2.2), round(headCenterY + headH * 0.06),
		                  round(headCenterX + depthShift * headW * 0.2), round(headCenterY + headH * 0.16), color);
	}

	float chinY = topY + headH * 0.95;
	float chinX = headCenterX + depthShift * 3.0;
	spr->fillTriangle(round(headCenterX - headW * 0.40), round(headCenterY + headH * 0.10),
	                  round(headCenterX + headW * 0.40), round(headCenterY + headH * 0.10),
	                  round(chinX), round(chinY), color);

	float neckTopY = chinY - headH * 0.14;
	float shoulderY = topY + headH * 1.40;
	float neckHalfW = max(2.5f, (H * 0.20f) * (0.52f + 0.48f * wScale));
	float shoulderHalfW = max(6.0f, (H * 0.90f) * wScale);

	float neckBaseX = cx - depthShift * 1.5;
	fillQuadTarget(spr,
	               headCenterX - neckHalfW, neckTopY,
	               headCenterX + neckHalfW, neckTopY,
	               neckBaseX + neckHalfW * 1.25, shoulderY,
	               neckBaseX - neckHalfW * 1.25, shoulderY, color);

	spr->fillTriangle(round(neckBaseX - neckHalfW), round(neckTopY + headH * 0.18),
	                  round(cx - shoulderHalfW * 0.92), round(shoulderY + 2),
	                  round(neckBaseX), round(shoulderY + 2), color);
	spr->fillTriangle(round(neckBaseX + neckHalfW), round(neckTopY + headH * 0.18),
	                  round(cx + shoulderHalfW * 0.92), round(shoulderY + 2),
	                  round(neckBaseX), round(shoulderY + 2), color);

	// 2. Organic Curved Torso
	float chestY = topY + headH * 2.02;
	float waistY = topY + headH * 2.84;
	float hipY = topY + headH * 3.64;

	float chestHalfW = max(5.5f, (H * 0.75f) * wScale);
	float waistHalfW = max(4.2f, (H * 0.52f) * wScale);
	float hipHalfW = max(5.5f, (H * 0.67f) * wScale);

	fillQuadTarget(spr, cx - shoulderHalfW * 0.92, shoulderY, cx + shoulderHalfW * 0.92, shoulderY,
	               cx + chestHalfW, chestY, cx - chestHalfW, chestY, color);
	fillQuadTarget(spr, cx - chestHalfW, chestY, cx + chestHalfW, chestY,
	               cx + waistHalfW, waistY, cx - waistHalfW, waistY, color);
	fillQuadTarget(spr, cx - waistHalfW, waistY, cx + waistHalfW, waistY,
	               cx + hipHalfW, hipY, cx - hipHalfW, hipY, color);

	spr->fillEllipse(round(cx + depthShift * 2.0), round((shoulderY + chestY) * 0.5),
	                 max(4, (int)round(chestHalfW * 1.02)), max(4, (int)round((chestY - shoulderY) * 0.55)), color);
	spr->fillEllipse(round(cx - depthShift * 1.5), round((waistY + hipY) * 0.55),
	                 max(4, (int)round(hipHalfW * 1.02)), max(4, (int)round((hipY - waistY) * 0.55)), color);

	float deltR = max(2.8f, (H * 0.21f) * (0.55f + 0.45f * wScale));
	float lShX = cx - shoulderHalfW * 0.92;
	float rShX = cx + shoulderHalfW * 0.92;
	float shJointY = shoulderY + deltR * 0.55;
	spr->fillCircle(round(lShX), round(shJointY), max(2, (int)round(deltR)), color);
	spr->fillCircle(round(rShX), round(shJointY), max(2, (int)round(deltR)), color);

	float lHipX = cx - hipHalfW * 0.60;
	float rHipX = cx + hipHalfW * 0.60;
	float hipJointY = hipY - H * 0.08 * hScale;

	float rUpperArm = max(2.2f, (H * 0.17f) * (0.55f + 0.45f * wScale));
	float rElbow = max(1.8f, (H * 0.13f) * (0.55f + 0.45f * wScale));
	float rWrist = max(1.3f, (H * 0.09f) * (0.55f + 0.45f * wScale));

	float rThigh = max(3.0f, (H * 0.27f) * (0.55f + 0.45f * wScale));
	float rKnee = max(2.2f, (H * 0.18f) * (0.55f + 0.45f * wScale));
	float rAnkle = max(1.6f, (H * 0.12f) * (0.55f + 0.45f * wScale));

	// 3. Articulated Limbs, Hands & Feet by Pose
	if (pose == 0) {
		float lElbX = lShX - H * 0.13 * wScale;
		float lElbY = shJointY + headH * 1.15;
		float lWriX = lShX - H * 0.05 * wScale + depthShift * 3.0;
		float lWriY = lElbY + headH * 1.02;

		float rElbX = rShX + H * 0.13 * wScale;
		float rElbY = shJointY + headH * 1.15;
		float rWriX = rShX + H * 0.05 * wScale + depthShift * 3.0;
		float rWriY = rElbY + headH * 1.02;

		drawTaperedLimb(spr, lShX, shJointY, lElbX, lElbY, rUpperArm, rElbow, color);
		drawTaperedLimb(spr, lElbX, lElbY, lWriX, lWriY, rElbow, rWrist, color);
		drawTaperedLimb(spr, lWriX, lWriY, lWriX + depthShift * 2.0, lWriY + headH * 0.36, rWrist * 1.1, rWrist * 0.5, color);

		drawTaperedLimb(spr, rShX, shJointY, rElbX, rElbY, rUpperArm, rElbow, color);
		drawTaperedLimb(spr, rElbX, rElbY, rWriX, rWriY, rElbow, rWrist, color);
		drawTaperedLimb(spr, rWriX, rWriY, rWriX + depthShift * 2.0, rWriY + headH * 0.36, rWrist * 1.1, rWrist * 0.5, color);

		float lKneeX = lHipX - H * 0.07 * wScale;
		float lKneeY = hipJointY + headH * 1.75;
		float lAnkX = lHipX - H * 0.12 * wScale;
		float lAnkY = lKneeY + headH * 1.65;

		float rKneeX = rHipX + H * 0.07 * wScale;
		float rKneeY = hipJointY + headH * 1.75;
		float rAnkX = rHipX + H * 0.12 * wScale;
		float rAnkY = rKneeY + headH * 1.65;

		drawTaperedLimb(spr, lHipX, hipJointY, lKneeX, lKneeY, rThigh, rKnee, color);
		drawTaperedLimb(spr, lKneeX, lKneeY, lAnkX, lAnkY, rKnee, rAnkle, color);
		drawTaperedLimb(spr, rHipX, hipJointY, rKneeX, rKneeY, rThigh, rKnee, color);
		drawTaperedLimb(spr, rKneeX, rKneeY, rAnkX, rAnkY, rKnee, rAnkle, color);

		drawTaperedLimb(spr, lAnkX, lAnkY, lAnkX - H * 0.22 * wScale + depthShift * 5.0, lAnkY + headH * 0.16, rAnkle * 1.1, rAnkle * 0.6, color);
		drawTaperedLimb(spr, rAnkX, rAnkY, rAnkX + H * 0.22 * wScale + depthShift * 5.0, rAnkY + headH * 0.16, rAnkle * 1.1, rAnkle * 0.6, color);

	} else if (pose == 1) {
		float lElbX = lShX - H * 0.20 * wScale - depthShift * 4.0;
		float lElbY = shJointY + headH * 1.05;
		float lWriX = lElbX + H * 0.14 * wScale - depthShift * 5.0;
		float lWriY = lElbY + headH * 0.88;

		float rElbX = rShX + H * 0.18 * wScale + depthShift * 4.0;
		float rElbY = shJointY + headH * 1.10;
		float rWriX = rElbX - H * 0.10 * wScale + depthShift * 5.0;
		float rWriY = rElbY + headH * 0.92;

		drawTaperedLimb(spr, lShX, shJointY, lElbX, lElbY, rUpperArm, rElbow, color);
		drawTaperedLimb(spr, lElbX, lElbY, lWriX, lWriY, rElbow, rWrist, color);
		drawTaperedLimb(spr, lWriX, lWriY, lWriX - depthShift * 2.0, lWriY + headH * 0.34, rWrist * 1.1, rWrist * 0.5, color);

		drawTaperedLimb(spr, rShX, shJointY, rElbX, rElbY, rUpperArm, rElbow, color);
		drawTaperedLimb(spr, rElbX, rElbY, rWriX, rWriY, rElbow, rWrist, color);
		drawTaperedLimb(spr, rWriX, rWriY, rWriX + depthShift * 2.0, rWriY + headH * 0.34, rWrist * 1.1, rWrist * 0.5, color);

		float lKneeX = lHipX - H * 0.30 * wScale - depthShift * 4.0;
		float lKneeY = hipJointY + headH * 1.65;
		float lAnkX = lHipX - H * 0.46 * wScale - depthShift * 6.0;
		float lAnkY = lKneeY + headH * 1.60;

		float rKneeX = rHipX + H * 0.22 * wScale + depthShift * 4.0;
		float rKneeY = hipJointY + headH * 1.70;
		float rAnkX = rHipX + H * 0.50 * wScale + depthShift * 7.0;
		float rAnkY = rKneeY + headH * 1.48;

		drawTaperedLimb(spr, lHipX, hipJointY, lKneeX, lKneeY, rThigh, rKnee, color);
		drawTaperedLimb(spr, lKneeX, lKneeY, lAnkX, lAnkY, rKnee, rAnkle, color);
		drawTaperedLimb(spr, rHipX, hipJointY, rKneeX, rKneeY, rThigh, rKnee, color);
		drawTaperedLimb(spr, rKneeX, rKneeY, rAnkX, rAnkY, rKnee, rAnkle, color);
		drawTaperedLimb(spr, lAnkX, lAnkY, lAnkX - H * 0.24 * wScale + depthShift * 5.0, lAnkY + headH * 0.14, rAnkle * 1.1, rAnkle * 0.6, color);
		drawTaperedLimb(spr, rAnkX, rAnkY, rAnkX + H * 0.20 * wScale + depthShift * 5.0, rAnkY + headH * 0.20, rAnkle * 1.1, rAnkle * 0.6, color);
	} else if (pose == 2) {
		float lElbX = lShX - H * 0.24 * wScale;
		float lElbY = shJointY + headH * 1.05;
		float lWriX = cx - waistHalfW * 0.85 + depthShift * 4.0;
		float lWriY = waistY + headH * 0.15;
		float rElbX = rShX + H * 0.24 * wScale;
		float rElbY = shJointY + headH * 1.05;
		float rWriX = cx + waistHalfW * 0.85 + depthShift * 4.0;
		float rWriY = waistY + headH * 0.15;
		drawTaperedLimb(spr, lShX, shJointY, lElbX, lElbY, rUpperArm, rElbow, color);
		drawTaperedLimb(spr, lElbX, lElbY, lWriX, lWriY, rElbow, rWrist, color);
		drawTaperedLimb(spr, lWriX, lWriY, lWriX + H * 0.12 * wScale, lWriY + headH * 0.24, rWrist * 1.1, rWrist * 0.6, color);
		drawTaperedLimb(spr, rShX, shJointY, rElbX, rElbY, rUpperArm, rElbow, color);
		drawTaperedLimb(spr, rElbX, rElbY, rWriX, rWriY, rElbow, rWrist, color);
		drawTaperedLimb(spr, rWriX, rWriY, rWriX - H * 0.12 * wScale, rWriY + headH * 0.24, rWrist * 1.1, rWrist * 0.6, color);
		float lKneeX = lHipX - H * 0.22 * wScale;
		float lKneeY = hipJointY + headH * 1.68;
		float lAnkX = lHipX - H * 0.34 * wScale;
		float lAnkY = lKneeY + headH * 1.62;
		float rKneeX = rHipX + H * 0.22 * wScale;
		float rKneeY = hipJointY + headH * 1.68;
		float rAnkX = rHipX + H * 0.34 * wScale;
		float rAnkY = rKneeY + headH * 1.62;
		drawTaperedLimb(spr, lHipX, hipJointY, lKneeX, lKneeY, rThigh, rKnee, color);
		drawTaperedLimb(spr, lKneeX, lKneeY, lAnkX, lAnkY, rKnee, rAnkle, color);
		drawTaperedLimb(spr, rHipX, hipJointY, rKneeX, rKneeY, rThigh, rKnee, color);
		drawTaperedLimb(spr, rKneeX, rKneeY, rAnkX, rAnkY, rKnee, rAnkle, color);
		drawTaperedLimb(spr, lAnkX, lAnkY, lAnkX - H * 0.24 * wScale + depthShift * 4.0, lAnkY + headH * 0.15, rAnkle * 1.1, rAnkle * 0.6, color);
		drawTaperedLimb(spr, rAnkX, rAnkY, rAnkX + H * 0.24 * wScale + depthShift * 4.0, rAnkY + headH * 0.15, rAnkle * 1.1, rAnkle * 0.6, color);
	} else {
		float lElbX = lShX - H * 0.26 * wScale;
		float lElbY = shJointY + headH * 0.95;
		float lWriX = cx - waistHalfW * 0.92;
		float lWriY = waistY + headH * 0.10;
		float rElbX = rShX + H * 0.17 * wScale;
		float rElbY = shJointY + headH * 1.12;
		float rWriX = rShX + H * 0.04 * wScale + depthShift * 4.0;
		float rWriY = chestY + headH * 0.05;
		drawTaperedLimb(spr, lShX, shJointY, lElbX, lElbY, rUpperArm, rElbow, color);
		drawTaperedLimb(spr, lElbX, lElbY, lWriX, lWriY, rElbow, rWrist, color);
		drawTaperedLimb(spr, rShX, shJointY, rElbX, rElbY, rUpperArm, rElbow, color);
		drawTaperedLimb(spr, rElbX, rElbY, rWriX, rWriY, rElbow, rWrist, color);
		drawTaperedLimb(spr, rWriX, rWriY, rWriX - H * 0.06 * wScale, rWriY - headH * 0.28, rWrist * 1.1, rWrist * 0.5, color);
		float lKneeX = lHipX - H * 0.05 * wScale;
		float lKneeY = hipJointY + headH * 1.75;
		float lAnkX = lHipX - H * 0.08 * wScale;
		float lAnkY = lKneeY + headH * 1.65;
		float rKneeX = rHipX + H * 0.16 * wScale;
		float rKneeY = hipJointY + headH * 1.72;
		float rAnkX = rHipX + H * 0.26 * wScale;
		float rAnkY = rKneeY + headH * 1.62;
		drawTaperedLimb(spr, lHipX, hipJointY, lKneeX, lKneeY, rThigh, rKnee, color);
		drawTaperedLimb(spr, lKneeX, lKneeY, lAnkX, lAnkY, rKnee, rAnkle, color);
		drawTaperedLimb(spr, rHipX, hipJointY, rKneeX, rKneeY, rThigh, rKnee, color);
		drawTaperedLimb(spr, rKneeX, rKneeY, rAnkX, rAnkY, rKnee, rAnkle, color);
		drawTaperedLimb(spr, lAnkX, lAnkY, lAnkX - H * 0.20 * wScale + depthShift * 4.0, lAnkY + headH * 0.15, rAnkle * 1.1, rAnkle * 0.6, color);
		drawTaperedLimb(spr, rAnkX, rAnkY, rAnkX + H * 0.24 * wScale + depthShift * 4.0, rAnkY + headH * 0.15, rAnkle * 1.1, rAnkle * 0.6, color);
	}
}
void initCrtvGraphics(PanelLan* display) {
	tftPtr = display;
	for (int yi = 0; yi < YAW_STEPS; yi++) {
		for (int pi = 0; pi < PITCH_STEPS; pi++) {
			humanSprites[yi][pi] = new LGFX_Sprite(tftPtr);
			humanSprites[yi][pi]->setPsram(true);
			humanSprites[yi][pi]->setColorDepth(16);
			if (!humanSprites[yi][pi]->createSprite(HUMAN_SPRITE_W, HUMAN_SPRITE_H)) {
				Serial.printf("Sprite alloc failed [%d][%d]\n", yi, pi);
			}
		}
	}
}
void cacheSinglePose(int pose) {
	if (cachedPoseIndex == pose) return;
	for (int yi = 0; yi < YAW_STEPS; yi++) {
		float yawDeg = -80.0 + yi * 20.0;
		for (int pi = 0; pi < PITCH_STEPS; pi++) {
			float pitchDeg = -30.0 + pi * 30.0;
			if (humanSprites[yi][pi]) {
				renderHumanPoseToSprite(humanSprites[yi][pi], pose, yawDeg, pitchDeg, SILHOUETTE_COLOR);
			}
		}
	}
	cachedPoseIndex = pose;
}
void drawCachedHumanSilhouette(int cx, int cy, float relYawDeg, float relPitchDeg, float scale) {
	if (!tftPtr) return;
	// Brief CRT signal dropout (~6% of frames the figure flickers out into static)
	if (random(0, 100) < 6) {
		return;
	}
	int yi = constrain((int)round((relYawDeg + 80.0) / 20.0), 0, YAW_STEPS - 1);
	int pi = constrain((int)round((relPitchDeg + 30.0) / 30.0), 0, PITCH_STEPS - 1);
	LGFX_Sprite* spr = humanSprites[yi][pi];
	if (!spr || spr->width() <= 0) return;
	int jitterX = (random(0, 100) < 25) ? random(-2, 3) : 0;
	int jitterY = (random(0, 100) < 20) ? random(-1, 2) : 0;
	spr->setPivot(HUMAN_SPRITE_W / 2.0, HUMAN_SPRITE_H / 2.0);
	spr->pushRotateZoom(tftPtr, cx + jitterX, cy + jitterY, 0.0, scale, scale, TRANS_COLOR);
	// Horizontal CRT sync-tear bars slicing across the silhouette
	if (random(0, 100) < 35) {
		int halfW = round((HUMAN_SPRITE_W * scale) * 0.55);
		int halfH = round((HUMAN_SPRITE_H * scale) * 0.48);
		int numSlices = random(1, 4);
		for (int s = 0; s < numSlices; s++) {
			int sliceY = cy - halfH + random(0, halfH * 2);
			int sliceH = random(2, 6);
			int shiftX = random(-14, 15);
			uint16_t tearColor = (random(0, 2) == 0) ? 0x2000 : 0x2945;
			tftPtr->fillRect(cx - halfW + shiftX, sliceY, halfW * 2, sliceH, tearColor);
		}
	}
}
void drawPivotingOpenParen(int pivotX, int pivotY, float orbitRadius, float angleDeg, uint16_t color) {
	if (!tftPtr) return;
	const float arcRadius = 46.0;
	const float maxHalfThick = 7.5;
	const float halfSweepDeg = 52.0;
	const int segments = 18;
	float dirRad = angleDeg * PI / 180.0;
	float dirX = sin(dirRad);
	float dirY = -cos(dirRad);
	float apexX = pivotX + dirX * orbitRadius;
	float apexY = pivotY + dirY * orbitRadius;
	float arcCenterX = apexX - dirX * arcRadius;
	float arcCenterY = apexY - dirY * arcRadius;
	float startRad = (angleDeg - halfSweepDeg) * PI / 180.0;
	float totalRad = 2.0 * halfSweepDeg * PI / 180.0;
	float stepRad = totalRad / segments;
	const uint16_t BLOOM_COLOR = 0x6180;
	for (int i = 0; i < segments; i++) {
		float t0 = (float)i / segments;
		float t1 = (float)(i + 1) / segments;
		float thick0 = (maxHalfThick + 3.0) * sin(t0 * PI);
		float thick1 = (maxHalfThick + 3.0) * sin(t1 * PI);
		float a0 = startRad + i * stepRad;
		float a1 = a0 + stepRad;
		float s0 = sin(a0), c0 = -cos(a0);
		float s1 = sin(a1), c1 = -cos(a1);
		fillQuadTarget(tftPtr,
		               arcCenterX + (arcRadius - thick0) * s0, arcCenterY + (arcRadius - thick0) * c0,
		               arcCenterX + (arcRadius + thick0) * s0, arcCenterY + (arcRadius + thick0) * c0,
		               arcCenterX + (arcRadius + thick1) * s1, arcCenterY + (arcRadius + thick1) * c1,
		               arcCenterX + (arcRadius - thick1) * s1, arcCenterY + (arcRadius - thick1) * c1,
		               BLOOM_COLOR);
	}
	for (int i = 0; i < segments; i++) {
		float t0 = (float)i / segments;
		float t1 = (float)(i + 1) / segments;
		float thick0 = maxHalfThick * sin(t0 * PI);
		float thick1 = maxHalfThick * sin(t1 * PI);
		float a0 = startRad + i * stepRad;
		float a1 = a0 + stepRad;
		float s0 = sin(a0), c0 = -cos(a0);
		float s1 = sin(a1), c1 = -cos(a1);
		fillQuadTarget(tftPtr,
		               arcCenterX + (arcRadius - thick0) * s0, arcCenterY + (arcRadius - thick0) * c0,
		               arcCenterX + (arcRadius + thick0) * s0, arcCenterY + (arcRadius + thick0) * c0,
		               arcCenterX + (arcRadius + thick1) * s1, arcCenterY + (arcRadius + thick1) * c1,
		               arcCenterX + (arcRadius - thick1) * s1, arcCenterY + (arcRadius - thick1) * c1,
		               color);
	}
}
void drawCRTVBezel() {
	if (!tftPtr) return;
	tftPtr->drawRoundRect(0, 0, SCREEN_W, SCREEN_H, 28, TFT_BLACK);
	tftPtr->drawRoundRect(1, 1, SCREEN_W - 2, SCREEN_H - 2, 27, TFT_BLACK);
	tftPtr->drawRoundRect(2, 2, SCREEN_W - 4, SCREEN_H - 4, 26, 0x1800);
}