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

INSTANCE_TYPE Instance::GetInstanceTypeForBiome(BIOME_TYPE biomeType, float d, PosInt3 worldPos)
{
	const std::vector<INSTANCE_TYPE> biomeInstances = Biome::GetInstances(biomeType);
	int worldY = std::get<1>(worldPos);

	switch (biomeType) {
	case BIOME_OCEAN:
		if (worldY < 52 && d < 0.5f)
			return biomeInstances[0]; // seagrass
		else
			return biomeInstances[1]; // kelp

	case BIOME_TUNDRA:
		return biomeInstances[0]; // none

	case BIOME_TAIGA:
		if (d < 0.5f)
			return biomeInstances[0]; // grass
		else if (d < 0.8f)
			return biomeInstances[1]; // fern, large fern
		else
			return biomeInstances[2]; // sweet berry bush

	case BIOME_PLAINS:
		if (d < 0.7f)
			return biomeInstances[0]; // grass
		else if (d < 0.83f)
			return biomeInstances[Utils::RandomRangeByPos(worldPos, 1, 2)]; // oxeye, conrflower
		else
			return biomeInstances[Utils::RandomRangeByPos(worldPos, 3, 6)]; // tulips

	case BIOME_SWAMP:
		if (d < 0.8f)
			return biomeInstances[0]; // grass
		else if (d < 0.9f)
			return biomeInstances[1]; // blue orchild
		else if (d < 0.95f)
			return biomeInstances[Utils::RandomRangeByPos(worldPos, 2, 3)]; // mushrooms
		else
			return biomeInstances[4]; // dead bush

	case BIOME_FOREST:
		if (d < 0.7f)
			return biomeInstances[0]; // grass
		else if (d < 0.85f)
			return biomeInstances[Utils::RandomRangeByPos(worldPos, 1, 3)]; // Rose blue,red,plants
		else
			return biomeInstances[Utils::RandomRangeByPos(worldPos, 4, 5)]; // lily, allium

	case BIOME_SHRUBLAND:
		if (d < 0.6f)
			return biomeInstances[0]; // grass
		else if (d < 0.85f)
			return biomeInstances[Utils::RandomRangeByPos(
				worldPos, 1, 4)]; // dandelion, cornflower, allium, oxeye daisy
		else
			return biomeInstances[Utils::RandomRangeByPos(worldPos, 5, 8)]; // tulips

	case BIOME_DESERT:
		return biomeInstances[0]; // dead bush

	case BIOME_RAINFOREST:
		if (d < 0.6f)
			return biomeInstances[0]; // grass
		else
			return biomeInstances[1]; // fern

	case BIOME_SEASONFOREST:
		if (d < 0.7f)
			return biomeInstances[0]; // grass
		else if (d < 0.85f)
			return biomeInstances[Utils::RandomRangeByPos(
				worldPos, 1, 3)]; // Allium, Lily, Rosebush
		else
			return biomeInstances[Utils::RandomRangeByPos(worldPos, 4, 7)]; // tulips

	case BIOME_SAVANNA:
		return biomeInstances[0]; // grass

	case BIOME_SNOWY_TAIGA:
		if (d < 0.7f)
			return biomeInstances[0]; // grass
		else if (d < 0.92f)
			return biomeInstances[1]; // fern
		else
			return biomeInstances[2]; // sweet berry bush
	}

	return INSTANCE_TYPE::INSTANCE_NONE;
}

bool Instance::CanPlace(INSTANCE_TYPE type, BLOCK_TYPE currentBlock, BLOCK_TYPE bottomBlock)
{
	if (!Block::IsTransparency(currentBlock))
		return false;

	if (type == INSTANCE_TYPE::INSTANCE_KELP && currentBlock != BLOCK_TYPE::BLOCK_WATER) {
		return false;
	}

	if (type == INSTANCE_TYPE::INSTANCE_WATER_LILY) {
		return (currentBlock == BLOCK_TYPE::BLOCK_AIR && bottomBlock == BLOCK_TYPE::BLOCK_WATER);
	}

	if (bottomBlock == BLOCK_TYPE::BLOCK_DIRT)
		return true;
	if (bottomBlock == BLOCK_TYPE::BLOCK_GRASS)
		return true;
	if (bottomBlock == BLOCK_TYPE::BLOCK_SNOW_GRASS)
		return true;
	if (bottomBlock == BLOCK_TYPE::BLOCK_GRAVEL)
		return true;

	if (INSTANCE_TYPE::INSTANCE_DEAD_BUSH && bottomBlock == BLOCK_TYPE::BLOCK_SAND)
		return true;

	return false;
}

INSTANCE_TYPE Instance::GetInstanceTypeForWaterPlane(
	float temperature, float humidity, float distribution, PosInt3 worldPos)
{
	if (humidity > 0.77f && temperature > 0.5f && distribution > 0.75f)
		return INSTANCE_TYPE::INSTANCE_WATER_LILY;

	return INSTANCE_TYPE::INSTANCE_NONE;
}