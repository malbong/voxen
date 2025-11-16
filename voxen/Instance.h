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
		m_instanceTypeInfoSet[INSTANCE_TYPE::INSTANCE_SHORT_GRASS].Init(
			INSTANCE_SHAPE::INSTANCE_CROSS, TEXTURE_INDEX::TEXTURE_SHORT_GRASS);
	}

	inline const InstanceTypeInfo& GetInfo(INSTANCE_TYPE type) const
	{
		return m_instanceTypeInfoSet[type];
	}

private:
	std::vector<InstanceTypeInfo> m_instanceTypeInfoSet;
};