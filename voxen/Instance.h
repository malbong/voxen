#pragma once

#include "Structure.h"

#include <stdint.h>
#include <vector>
#include <directxtk/SimpleMath.h>

using namespace DirectX::SimpleMath;

class InstanceTypeInfo;
class InstanceTypeInfoSet;

class Instance {

public:
	static const uint32_t INSTANCE_TYPE_COUNT = 256;

	static INSTANCE_SHAPE GetShape(INSTANCE_TYPE type);
	static TEXTURE_INDEX GetTextureIndex(INSTANCE_TYPE type);
	static INSTANCE_TYPE GetInstanceTypeForBiome(
		BIOME_TYPE biomeType, int worldX, int worldZ, int solt);

	Instance() : m_type(INSTANCE_TYPE::INSTANCE_SHORT_GRASS) {}
	Instance(INSTANCE_TYPE type) : m_type(type) {}
	~Instance() {}

	inline INSTANCE_TYPE GetType() const { return m_type; }
	inline void SetType(INSTANCE_TYPE type) { m_type = type; }

private:
	static InstanceTypeInfoSet m_instanceTypeInfoSet;

	INSTANCE_TYPE m_type;
};


class InstanceTypeInfo {

public:
	InstanceTypeInfo()
		: m_shape(INSTANCE_SHAPE::INSTANCE_CROSS), m_texIndex(TEXTURE_INDEX::TEXTURE_SHORT_GRASS)
	{
	}
	InstanceTypeInfo(INSTANCE_SHAPE shape, TEXTURE_INDEX texIndex)
		: m_shape(shape), m_texIndex(texIndex)
	{
	}
	~InstanceTypeInfo() {}

	inline void Init(INSTANCE_SHAPE shape, TEXTURE_INDEX texIndex)
	{
		m_shape = shape;
		m_texIndex = texIndex;
	}
	inline INSTANCE_SHAPE GetShape() const { return m_shape; }
	inline TEXTURE_INDEX GetTextureIndex() const { return m_texIndex; }

private:
	INSTANCE_SHAPE m_shape;
	TEXTURE_INDEX m_texIndex;
};


class InstanceTypeInfoSet {

public:
	InstanceTypeInfoSet() : m_instanceTypeInfoSet(Instance::INSTANCE_TYPE_COUNT)
	{
		// INSTANCE_TYPE, TEXTURE_TYPE
		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_SHORT_GRASS].Init(
			INSTANCE_SHAPE::INSTANCE_CROSS, TEXTURE_INDEX::TEXTURE_SHORT_GRASS);

		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_SEAGRASS].Init(
			INSTANCE_SHAPE::INSTANCE_CROSS, TEXTURE_INDEX::TEXTURE_SEAGRASS);

		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_KELP].Init(
			INSTANCE_SHAPE::INSTANCE_CROSS, TEXTURE_INDEX::TEXTURE_KELP);

		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_FERN].Init(
			INSTANCE_SHAPE::INSTANCE_CROSS, TEXTURE_INDEX::TEXTURE_FERN);

		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_SWEET_BERRY_BUSH].Init(
			INSTANCE_SHAPE::INSTANCE_CROSS, TEXTURE_INDEX::TEXTURE_SWEET_BERRY_BUSH);

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
			INSTANCE_SHAPE::INSTANCE_CROSS, TEXTURE_INDEX::TEXTURE_ROSE_PLANTS);

		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_LILY_OF_THE_VALLEY].Init(
			INSTANCE_SHAPE::INSTANCE_CROSS, TEXTURE_INDEX::TEXTURE_LILY_OF_THE_VALLEY);

		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_ALLIUM].Init(
			INSTANCE_SHAPE::INSTANCE_CROSS, TEXTURE_INDEX::TEXTURE_ALLIUM);

		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_LARGE_FERN].Init(
			INSTANCE_SHAPE::INSTANCE_CROSS, TEXTURE_INDEX::TEXTURE_LARGE_FERN);

		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_DANDELION].Init(
			INSTANCE_SHAPE::INSTANCE_CROSS, TEXTURE_INDEX::TEXTURE_DANDELION);
	}

	inline const InstanceTypeInfo& GetInfo(INSTANCE_TYPE type) const
	{
		return m_instanceTypeInfoSet[type];
	}

private:
	std::vector<InstanceTypeInfo> m_instanceTypeInfoSet;
};