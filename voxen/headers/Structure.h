#pragma once

#include "Enums.h"

#include <directxtk/SimpleMath.h>
#include <unordered_map>
#include <unordered_set>

using namespace DirectX::SimpleMath;

struct RGBA_UINT {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
};

struct CLIMATE {
	float t;
	float h;
};

struct VoxelVertex {
	uint32_t data;
};

struct SkyboxVertex {
	Vector3 position; 
};

struct CloudVertex {
	Vector3 position;
	uint8_t face;
};

struct SamplingVertex {
	Vector3 position;
	Vector2 texcoord;
};

struct InstanceVertex {
	Vector3 position;
	Vector3 normal;
	Vector2 texcoord;
};

struct InstanceInfoVertex {
	Matrix instanceWorld;
	uint32_t texIndex;
};

struct PickingObjectVertex {
	Vector3 position;
	Vector3 color;
};

struct CameraConstantData {
	Matrix view;
	Matrix proj;
	Matrix invProj;
	Vector3 eyePos;
	float maxRenderDistance;
	Vector3 eyeDir;
	float lodRenderDistance;
	uint32_t isUnderWater;
	Vector3 dummy;
};

struct ChunkConstantData {
	Matrix world;
};

struct SkyboxConstantData {
	Vector3 normalHorizonColor;
	float skyScale;
	Vector3 normalZenithColor;
	float dummy1;
	Vector3 sunHorizonColor;
	float dummy2;
	Vector3 sunZenithColor;
	float dummy3;
};

struct LightConstantData {
	Vector3 lightDir;
	float radianceWeight;
	Vector3 radianceColor;
	float maxRadianceWeight;
};

struct CloudConstantData {
	Matrix world;
	Vector3 volumeColor;
	float cloudScale;
};

struct BlurConstantData {
	float dx;
	float dy;
	Vector2 dummy;
};

struct ShadowConstantData {
	Matrix viewProj[3];
	float topLX[4];
	float viewportWidth[4];
};

struct FogFilterConstantData {
	float fogDistMin;
	float fogDistMax;
	float fogStrength;
	float dummy1;
	Vector3 fogColor;
	float dummy2;
};

struct WaterFilterConstantData {
	Vector3 filterColor;
	float filterStrength;
};

struct SsaoConstantData {
	Vector4 sampleKernel[64];
};

struct SsaoNoiseConstantData {
	Vector4 rotationNoise[16];
};

struct AppConstantData {
	float appWidth;
	float appHeight;
	float mirrorWidth;
	float mirrorHeight;
};

struct DateConstantData {
	uint32_t days;
	uint32_t dateTime;
	uint32_t dayCycleRealTime;
	uint32_t dayCycleAmount;
};