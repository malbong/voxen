#include "Instance.h"

InstanceTypeInfoSet Instance::m_instanceTypeInfoSet;

INSTANCE_SHAPE Instance::GetShape(INSTANCE_TYPE type)
{
	return m_instanceTypeInfoSet.GetInfo(type).GetShape();
}

TEXTURE_INDEX Instance::GetTextureIndex(INSTANCE_TYPE type)
{
	return m_instanceTypeInfoSet.GetInfo(type).GetTextureIndex();
}

INSTANCE_TYPE Instance::GetInstanceTypeForBiome(BIOME_TYPE biomeType, int worldX, int worldZ, int solt)
{
	// worldX worldZ solt ·Î ·£´ý »Ì±â
	return INSTANCE_TYPE::INSTANCE_SHORT_GRASS;
}