#pragma once

#include <stdint.h>
#include <directxtk/SimpleMath.h>

using namespace DirectX::SimpleMath;

class Instance {
public:
	Instance(INSTANCE_SHAPE shape, TEXTURE_INDEX texIndex)
		 : m_shape(shape), m_texIndex(texIndex)
	{
	}

	~Instance() {}

	inline INSTANCE_SHAPE GetShape() const { return m_shape; }
	inline TEXTURE_INDEX GetTextureIndex() const { return m_texIndex; }

private:
	INSTANCE_SHAPE m_shape;
	TEXTURE_INDEX m_texIndex;
};


class InstanceGroup {
public:
	Instance shortGrassInstance;

	InstanceGroup()
		: shortGrassInstance(INSTANCE_SHAPE::INSTANCE_CROSS, TEXTURE_INDEX::TEXTURE_SHORT_GRASS)
	{
	}

	inline const Instance* GetShortGrass() const { return &shortGrassInstance; }
};