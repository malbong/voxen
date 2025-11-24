#include "Instance.h"
#include "Biome.h"
#include "Utils.h"

InstanceTypeInfoSet Instance::m_instanceTypeInfoSet;

INSTANCE_SHAPE Instance::GetShape(INSTANCE_TYPE type)
{
	return m_instanceTypeInfoSet.GetInfo(type).GetShape();
}

TEXTURE_INDEX Instance::GetTextureIndex(INSTANCE_TYPE type)
{
	
	return m_instanceTypeInfoSet.GetInfo(type).GetTextureIndex();
}

TEXTURE_INDEX Instance::GetTextureTopIndex(INSTANCE_TYPE type)
{
	return m_instanceTypeInfoSet.GetInfo(type).GetTextureTopIndex();
}

TEXTURE_INDEX Instance::GetTextureBottomIndex(INSTANCE_TYPE type)
{
	return m_instanceTypeInfoSet.GetInfo(type).GetTextureBottomIndex();
}

TEXTURE_INDEX Instance::GetTextureIndexByHeight(
	INSTANCE_TYPE type, int currentHeight, int maxHeight)
{
	if (maxHeight == 1) {
		return m_instanceTypeInfoSet.GetInfo(type).GetTextureIndex();
	}
	else {
		if (currentHeight == maxHeight - 1) {
			return m_instanceTypeInfoSet.GetInfo(type).GetTextureTopIndex();
		}
		else {
			return m_instanceTypeInfoSet.GetInfo(type).GetTextureBottomIndex();
		}
	}
}

uint8_t Instance::GetMaxHeight(INSTANCE_TYPE type)
{
	return m_instanceTypeInfoSet.GetInfo(type).GetMaxHeight();
}

INSTANCE_TYPE Instance::GetInstanceTypeForBiome(
	BIOME_TYPE biomeType, float d, int localX, int localY, int localZ)
{
	const std::vector<INSTANCE_TYPE> biomeInstances = Biome::GetInstances(biomeType);
	uint32_t hash = Utils::HashInt((uint32_t)(localX * localZ), localY);
	
	switch (biomeType)
	{
	case BIOME_OCEAN:
		if (d < 0.65f)
			return biomeInstances[0]; // shortseagrass
		else
			return biomeInstances[1]; // kelp

	case BIOME_BEACH:
		return biomeInstances[0]; // none

	case BIOME_TUNDRA:
		return biomeInstances[0]; // none

	case BIOME_TAIGA:
		if (d < 0.5f)
			return biomeInstances[0]; // grass
		else if (d < 0.8f)
			return biomeInstances[hash % 2 + 1]; // fern, large fern
		else
			return biomeInstances[3]; // sweet berry bush

	case BIOME_PLAINS:
		if (d < 0.7f)
			return biomeInstances[0]; // grass
		else if (d < 0.83f)
			return biomeInstances[hash % 2 + 1]; // oxeyeDaisy, conrflower
		else
			return biomeInstances[hash % 4 + 3]; // tulips

	case BIOME_SWAMP:
		if (d < 0.8f)
			return biomeInstances[0]; // grass
		else if (d < 0.85f)
			return biomeInstances[1]; // seagrass
		else if (d < 0.9f)
			return biomeInstances[2]; // blue orchild
		else if (d < 0.95f)
			return biomeInstances[hash % 2 + 3]; // mushrooms
		else
			return biomeInstances[5]; // dead bush

	case BIOME_FOREST:
		if (d < 0.7f)
			return biomeInstances[0]; // grass
		else if (d < 0.85f)
			return biomeInstances[hash % 3 + 1]; // Rose blue,red,plants
		else
			return biomeInstances[hash % 2 + 4]; // lily, allium

	case BIOME_SHRUBLAND:
		if (d < 0.6f)
			return biomeInstances[0]; // grass
		else if (d < 0.85f)
			return biomeInstances[hash % 4 + 1]; // dandelion, cornflower, allium, oxeye daisy
		else
			return biomeInstances[hash % 4 + 5]; // tulips

	case BIOME_DESERT:
		return biomeInstances[0]; // dead bush

	case BIOME_RAINFOREST:
		if (d < 0.6f)
			return biomeInstances[0]; // grass
		else
			return biomeInstances[hash % 2 + 1]; // fern, large fern

	case BIOME_SEASONFOREST:
		if (d < 0.7f)
			return biomeInstances[0]; // grass
		else if (d < 0.85f)
			return biomeInstances[hash % 3 + 1]; // Allium, Lily, Rosebush
		else
			return biomeInstances[hash % 4 + 4]; // tulips

	case BIOME_SAVANA:
		return biomeInstances[0]; // grass

	case BIOME_SNOWY_TAIGA:
		if (d < 0.7f)
			return biomeInstances[0]; // grass
		else if (d < 0.92f)
			return biomeInstances[hash % 2 + 1]; // fern, large fern
		else
			return biomeInstances[3]; // sweet berry bush
	}

	return INSTANCE_TYPE::INSTANCE_NONE;
}