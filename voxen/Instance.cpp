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

INSTANCE_TYPE Instance::GetInstanceTypeForBiome(
	BIOME_TYPE biomeType, float d, int localX, int localY, int localZ)
{
	const std::vector<Instance> biomeInstances = Biome::GetInstances(biomeType);
	uint32_t hash = Utils::HashInt((uint32_t)(localX * localZ), localY);
	
	switch (biomeType)
	{
	case BIOME_OCEAN:
		if (d < 0.65f)
			return biomeInstances[0].GetType(); // seagrass
		else
			return biomeInstances[1].GetType(); // kelp

	case BIOME_BEACH:
		return biomeInstances[0].GetType(); // none

	case BIOME_TUNDRA:
		return biomeInstances[0].GetType(); // none

	case BIOME_TAIGA:
		if (d < 0.5f)
			return biomeInstances[0].GetType(); // grass
		else if (d < 0.8f)
			return biomeInstances[hash % 2 + 1].GetType(); // fern, large fern
		else
			return biomeInstances[3].GetType(); // sweet berry bush

	case BIOME_PLAINS:
		if (d < 0.7f)
			return biomeInstances[0].GetType(); // grass
		else if (d < 0.83f)
			return biomeInstances[hash % 2 + 1].GetType(); // oxeyeDaisy, conrflower
		else
			return biomeInstances[hash % 4 + 3].GetType(); // tulips

	case BIOME_SWAMP:
		if (d < 0.8f)
			return biomeInstances[0].GetType(); // grass
		else if (d < 0.85f)
			return biomeInstances[1].GetType(); // seagrass
		else if (d < 0.9f)
			return biomeInstances[2].GetType(); // blue orchild
		else if (d < 0.95f)
			return biomeInstances[hash % 2 + 3].GetType(); // mushrooms
		else
			return biomeInstances[5].GetType(); // dead bush

	case BIOME_FOREST:
		if (d < 0.7f)
			return biomeInstances[0].GetType(); // grass
		else if (d < 0.85f)
			return biomeInstances[hash % 3 + 1].GetType(); // Rose blue,red,plants
		else
			return biomeInstances[hash % 2 + 4].GetType(); // lily, allium
		break;

	case BIOME_SHRUBLAND:
		if (d < 0.6f)
			return biomeInstances[0].GetType(); // grass
		else if (d < 0.85f)
			return biomeInstances[hash % 4 + 1].GetType(); // dandelion, cornflower, allium, oxeye daisy
		else
			return biomeInstances[hash % 4 + 5].GetType(); // tulips

	case BIOME_DESERT:
		return biomeInstances[0].GetType(); // dead bush

	case BIOME_RAINFOREST:
		if (d < 0.6f)
			return biomeInstances[0].GetType(); // grass
		else
			return biomeInstances[hash % 2 + 1].GetType(); // fern, large fern
		break;

	case BIOME_SEASONFOREST:
		if (d < 0.7f)
			return biomeInstances[0].GetType(); // grass
		else if (d < 0.85f)
			return biomeInstances[hash % 3 + 1].GetType(); // Allium, Lily, Rosebush
		else
			return biomeInstances[hash % 4 + 4].GetType(); // tulips
		break;

	case BIOME_SAVANA:
		return biomeInstances[0].GetType(); // grass

	case BIOME_SNOWY_TAIGA:
		if (d < 0.7f)
			return biomeInstances[0].GetType(); // grass
		else if (d < 0.92f)
			return biomeInstances[hash % 2 + 1].GetType(); // fern, large fern
		else
			return biomeInstances[3].GetType(); // sweet berry bush
		break;
	}

	return INSTANCE_TYPE::INSTANCE_SHORT_GRASS;
}