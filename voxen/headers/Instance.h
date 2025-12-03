#pragma once

#include "Structure.h"
#include "Pos.h"
#include "Block.h"

#include <stdint.h>
#include <vector>

class InstanceTypeInfoSet;

class Instance {

public:
	static const uint32_t INSTANCE_TYPE_COUNT = 256;

	static INSTANCE_SHAPE GetShape(INSTANCE_TYPE type);
	static TEXTURE_INDEX GetTextureIndex(INSTANCE_TYPE type);
	static TEXTURE_INDEX GetTextureTopIndex(INSTANCE_TYPE type);
	static TEXTURE_INDEX GetTextureBottomIndex(INSTANCE_TYPE type);
	static TEXTURE_INDEX GetTextureIndexByHeight(
		INSTANCE_TYPE type, int currentHeight, int maxHeight);
	static uint8_t GetMaxHeight(INSTANCE_TYPE type);
	static INSTANCE_TYPE GetInstanceTypeForBiome(BIOME_TYPE biomeType, float d, PosInt3 worldPos);
	static bool CanPlace(INSTANCE_TYPE type, BLOCK_TYPE currentBlock, BLOCK_TYPE bottomBlock);
	static INSTANCE_TYPE GetInstanceTypeForWaterPlane(
		float temperature, float humidity, float distribution, PosInt3 worldPos);

	Instance()
		: m_type(INSTANCE_TYPE::INSTANCE_NONE), m_texIndex(TEXTURE_INDEX::TEXTURE_NONE),
		  m_yawRotation(0.0f), m_offsetNoisePositionXZ(0.0f), m_faceFlag(0)
	{
	}
	Instance(INSTANCE_TYPE type, TEXTURE_INDEX texIndex)
		: m_type(type), m_texIndex(texIndex), m_yawRotation(0.0f), m_offsetNoisePositionXZ(0.0f),
		  m_faceFlag(0)
	{
	}
	Instance(INSTANCE_TYPE type, TEXTURE_INDEX texIndex, float yawRotation,
		Vector2 offsetNoisePositionXZ)
		: m_type(type), m_texIndex(texIndex), m_yawRotation(yawRotation),
		  m_offsetNoisePositionXZ(offsetNoisePositionXZ), m_faceFlag(0)
	{
	}
	Instance(INSTANCE_TYPE type, TEXTURE_INDEX texIndex, float yawRotation,
		Vector2 offsetNoisePositionXZ, uint8_t faceFlag)
		: m_type(type), m_texIndex(texIndex), m_yawRotation(yawRotation),
		  m_offsetNoisePositionXZ(offsetNoisePositionXZ), m_faceFlag(faceFlag)
	{
	}
	~Instance() {}

	inline INSTANCE_TYPE GetType() const { return m_type; }
	inline void SetType(INSTANCE_TYPE type) { m_type = type; }

	inline TEXTURE_INDEX GetTexIndex() const { return m_texIndex; }
	inline void SetTexIndex(TEXTURE_INDEX texIndex) { m_texIndex = texIndex; }

	inline float GetYawRotation() const { return m_yawRotation; }
	inline void SetYawRotation(float yawRotation) { m_yawRotation = yawRotation; }

	inline Vector2 GetOffsetNoisePositionXZ() const { return m_offsetNoisePositionXZ; }
	inline void SetOffsetNoisePositionXZ(Vector2 offsetNoisePositionXZ)
	{
		m_offsetNoisePositionXZ = offsetNoisePositionXZ;
	}

	inline uint8_t GetFaceFlag() const { return m_faceFlag; }
	inline void SetFaceFlag(uint8_t faceFlag) { m_faceFlag = faceFlag; }

private:
	static InstanceTypeInfoSet m_instanceTypeInfoSet;

	INSTANCE_TYPE m_type;
	TEXTURE_INDEX m_texIndex;
	float m_yawRotation;
	Vector2 m_offsetNoisePositionXZ;
	uint8_t m_faceFlag;
};


class InstanceTypeInfo {

public:
	InstanceTypeInfo()
		: m_shape(INSTANCE_SHAPE::INSTANCE_CROSS), m_texIndex(TEXTURE_INDEX::TEXTURE_NONE),
		  m_texTopIndex(TEXTURE_INDEX::TEXTURE_NONE), m_texBottomIndex(TEXTURE_INDEX::TEXTURE_NONE),
		  m_maxHeight(1)
	{
	}
	InstanceTypeInfo(INSTANCE_SHAPE shape, TEXTURE_INDEX texIndex, TEXTURE_INDEX texTopIndex,
		TEXTURE_INDEX texBottomIndex, uint8_t maxHeight)
		: m_shape(shape), m_texIndex(texIndex), m_texTopIndex(texTopIndex),
		  m_texBottomIndex(texBottomIndex), m_maxHeight(maxHeight)
	{
	}
	~InstanceTypeInfo() {}

	inline void Init(INSTANCE_SHAPE shape, TEXTURE_INDEX texIndex)
	{
		m_shape = shape;
		m_texIndex = texIndex;
		m_texTopIndex = TEXTURE_INDEX::TEXTURE_NONE;
		m_texBottomIndex = TEXTURE_INDEX::TEXTURE_NONE;
		m_maxHeight = 1;
	}
	inline void Init(INSTANCE_SHAPE shape, TEXTURE_INDEX texIndex, TEXTURE_INDEX texTopIndex,
		TEXTURE_INDEX texBottomIndex, uint8_t maxHeight)
	{
		m_shape = shape;
		m_texIndex = texIndex;
		m_texTopIndex = texTopIndex;
		m_texBottomIndex = texBottomIndex;
		m_maxHeight = maxHeight;
	}
	inline INSTANCE_SHAPE GetShape() const { return m_shape; }
	inline TEXTURE_INDEX GetTextureIndex() const { return m_texIndex; }
	inline TEXTURE_INDEX GetTextureTopIndex() const { return m_texTopIndex; }
	inline TEXTURE_INDEX GetTextureBottomIndex() const { return m_texBottomIndex; }
	inline uint8_t GetMaxHeight() const { return m_maxHeight; }

private:
	INSTANCE_SHAPE m_shape;
	TEXTURE_INDEX m_texIndex;
	TEXTURE_INDEX m_texTopIndex;
	TEXTURE_INDEX m_texBottomIndex;
	uint8_t m_maxHeight;
};


class InstanceTypeInfoSet {

public:
	InstanceTypeInfoSet() : m_instanceTypeInfoSet(Instance::INSTANCE_TYPE_COUNT)
	{
		// INSTANCE_TYPE, TEXTURE_TYPE
		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_GRASS].Init(INSTANCE_SHAPE::INSTANCE_CROSS,
			TEXTURE_INDEX::TEXTURE_SHORT_GRASS, TEXTURE_INDEX::TEXTURE_DOUBLE_GRASS_TOP,
			TEXTURE_INDEX::TEXTURE_DOBULE_GRASS_BOTTOM, 2);

		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_SEAGRASS].Init(INSTANCE_SHAPE::INSTANCE_CROSS,
			TEXTURE_INDEX::TEXTURE_SEAGRASS, TEXTURE_INDEX::TEXTURE_DOUBLE_SEAGRASS_TOP,
			TEXTURE_INDEX::TEXTURE_DOUBLE_SEAGRASS_BOTTOM, 2);

		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_KELP].Init(INSTANCE_SHAPE::INSTANCE_CROSS,
			TEXTURE_INDEX::TEXTURE_KELP_TOP, TEXTURE_INDEX::TEXTURE_KELP_TOP,
			TEXTURE_INDEX::TEXTURE_KELP, 10);

		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_FERN].Init(INSTANCE_SHAPE::INSTANCE_CROSS,
			TEXTURE_INDEX::TEXTURE_FERN, TEXTURE_INDEX::TEXTURE_DOUBLE_FERN_TOP,
			TEXTURE_INDEX::TEXTURE_DOUBLE_FERN_BOTTOM, 3);

		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_SWEET_BERRY_BUSH].Init(
			INSTANCE_SHAPE::INSTANCE_FENCE, TEXTURE_INDEX::TEXTURE_SWEET_BERRY_BUSH);

		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_OXEYE_DAISY].Init(
			INSTANCE_SHAPE::INSTANCE_CROSS, TEXTURE_INDEX::TEXTURE_OXEYE_DAISY);

		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_CORN_FLOWER].Init(
			INSTANCE_SHAPE::INSTANCE_CROSS, TEXTURE_INDEX::TEXTURE_CORN_FLOWER);

		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_TULIP_PINK].Init(
			INSTANCE_SHAPE::INSTANCE_CROSS, TEXTURE_INDEX::TEXTURE_TULIP_PINK);

		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_TULIP_RED].Init(
			INSTANCE_SHAPE::INSTANCE_CROSS, TEXTURE_INDEX::TEXTURE_TULIP_RED);

		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_TULIP_WHITE].Init(
			INSTANCE_SHAPE::INSTANCE_CROSS, TEXTURE_INDEX::TEXTURE_TULIP_WHITE);

		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_TULIP_ORANGE].Init(
			INSTANCE_SHAPE::INSTANCE_CROSS, TEXTURE_INDEX::TEXTURE_TULIP_ORANGE);

		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_BLUE_ORCHID].Init(
			INSTANCE_SHAPE::INSTANCE_CROSS, TEXTURE_INDEX::TEXTURE_BLUE_ORCHID);

		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_MUSHROOM_BROWN].Init(
			INSTANCE_SHAPE::INSTANCE_CROSS, TEXTURE_INDEX::TEXTURE_MUSHROOM_BROWN);

		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_MUSHROOM_RED].Init(
			INSTANCE_SHAPE::INSTANCE_CROSS, TEXTURE_INDEX::TEXTURE_MUSHROOM_RED);

		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_DEAD_BUSH].Init(
			INSTANCE_SHAPE::INSTANCE_CROSS, TEXTURE_INDEX::TEXTURE_DEAD_BUSH);

		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_ROSE_BLUE].Init(
			INSTANCE_SHAPE::INSTANCE_CROSS, TEXTURE_INDEX::TEXTURE_ROSE_BLUE);

		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_ROSE_RED].Init(
			INSTANCE_SHAPE::INSTANCE_CROSS, TEXTURE_INDEX::TEXTURE_ROSE_RED);

		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_ROSE_PLANTS].Init(
			INSTANCE_SHAPE::INSTANCE_FENCE, TEXTURE_INDEX::TEXTURE_ROSE_PLANTS);

		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_LILY_OF_THE_VALLEY].Init(
			INSTANCE_SHAPE::INSTANCE_CROSS, TEXTURE_INDEX::TEXTURE_LILY_OF_THE_VALLEY);

		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_ALLIUM].Init(
			INSTANCE_SHAPE::INSTANCE_CROSS, TEXTURE_INDEX::TEXTURE_ALLIUM);

		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_DANDELION].Init(
			INSTANCE_SHAPE::INSTANCE_CROSS, TEXTURE_INDEX::TEXTURE_DANDELION);

		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_WATER_LILY].Init(
			INSTANCE_SHAPE::INSTANCE_FLOOR, TEXTURE_INDEX::TEXTURE_WATER_LILY);

		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_VINE].Init(
			INSTANCE_SHAPE::INSTANCE_SQUARE, TEXTURE_INDEX::TEXTURE_VINE);

		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_NONE].Init(
			INSTANCE_SHAPE::INSTANCE_CROSS, TEXTURE_INDEX::TEXTURE_NONE);
	}

	inline const InstanceTypeInfo& GetInfo(INSTANCE_TYPE type) const
	{
		return m_instanceTypeInfoSet[type];
	}

private:
	std::vector<InstanceTypeInfo> m_instanceTypeInfoSet;
};