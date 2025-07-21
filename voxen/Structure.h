#pragma once

#include <directxtk/SimpleMath.h>

using namespace DirectX::SimpleMath;

enum DIR : uint8_t {
	LEFT = 0,
	RIGHT = 1, 
	BOTTOM = 2,
	TOP = 3,
	FRONT = 4,
	BACK = 5,
	ANY = 6,
};

enum BIOME_TYPE : uint8_t {
	BIOME_OCEAN = 0,
	BIOME_BEACH = 1,
	BIOME_TUNDRA = 2,
	BIOME_TAIGA = 3,
	BIOME_PLAINS = 4,
	BIOME_SWAMP = 5,
	BIOME_FOREST = 6,
	BIOME_SHRUBLAND = 7,
	BIOME_DESERT = 8,
	BIOME_RAINFOREST = 9,
	BIOME_SEASONFOREST = 10,
	BIOME_SAVANA = 11,
	BIOME_SNOWY_TAIGA = 12,
};

enum BLOCK_TYPE : uint8_t {
	// block
	BLOCK_AIR = 0,
	BLOCK_WATER = 1,
	BLOCK_BEDROCK = 2,
	BLOCK_GRASS = 3,
	BLOCK_SNOW_GRASS = 4,
	BLOCK_DIRT = 5,
	BLOCK_STONE = 6,
	BLOCK_SAND = 7,
	BLOCK_SNOW = 8,
	BLOCK_GRAVEL = 9,
	BLOCK_SANDSTONE = 10,
	BLOCK_CLAY = 11,
	BLOCK_ANDESITE = 12,
	BLOCK_COAL_ORE = 13,
	BLOCK_GOLD_ORE = 14,
	BLOCK_REDSTONE_ORE = 15,
	BLOCK_DIAMOND_ORE = 16,
	BLOCK_COPPER_ORE = 17,
	BLOCK_IRON_ORE = 18,
	BLOCK_COARSE = 19,
	BLOCK_PODZOL = 20,
	BLOCK_ICE = 21,
	BLOCK_GOLD = 22,

	// instance
	BLOCK_SHORT_GRASS = 128
};

enum TEXTURE_INDEX : uint8_t {
	// block
	TEXTURE_WATER = 0,
	TEXTURE_GRASS_TOP = 1,
	TEXTURE_GRASS_OVERLAY = 2,
	TEXTURE_DIRT = 3,
	TEXTURE_SAND = 4,
	TEXTURE_BEDROCK = 5,
	TEXTURE_STONE = 6,
	TEXTURE_SNOW_GRASS_TOP = 7,
	TEXTURE_SNOW_GRASS_SIDE = 8,
	TEXTURE_SNOW = 9,
	TEXTURE_GRAVEL = 10,
	TEXTURE_SANDSTONE_SIDE = 11,
	TEXTURE_SANDSTONE_BOTTOM = 12,
	TEXTURE_SANDSTONE_TOP = 13,
	TEXTURE_CLAY = 14,
	TEXTURE_ANDESITE = 15,
	TEXTURE_GOLD_ORE = 16,
	TEXTURE_REDSTONE_ORE = 17,
	TEXTURE_DIAMOND_ORE = 18,
	TEXTURE_COPPER_ORE = 19,
	TEXTURE_IRON_ORE = 20,
	TEXTURE_COAL_ORE = 21,
	TEXTURE_COARSE = 22,
	TEXTURE_PODZOL_TOP = 23,
	TEXTURE_PODZOL_SIDE = 24,
	TEXTURE_ICE = 25,
	TEXTURE_GOLD = 26,

	// instance
	TEXTURE_SHORT_GRASS = 128
};

enum INSTANCE_TYPE : uint8_t {
	INSTANCE_CROSS = 0,
	INSTANCE_FENCE = 1,
	INSTANCE_SQUARE = 2,
	INSTANCE_NONE = 3,
};

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

struct PickingBlockVertex {
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