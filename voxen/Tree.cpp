#include "Tree.h"
#include "Biome.h"
#include "Utils.h"

TreeTypeInfoSet Tree::m_treeTypeInfoSet;

BLOCK_TYPE Tree::GetTrunkBlockType(TREE_TYPE type)
{
	return m_treeTypeInfoSet.GetInfo(type).GetTrunkBlockType();
}

BLOCK_TYPE Tree::GetLeafBlockType(TREE_TYPE type)
{
	return m_treeTypeInfoSet.GetInfo(type).GetLeafBlockType();
}

const std::vector<uint8_t> Tree::GetShape(TREE_TYPE type) 
{
	return m_treeTypeInfoSet.GetInfo(type).GetShape();
}

TREE_TYPE Tree::GetTreeTypeForBiome(
	BIOME_TYPE biomeType, float d, int localX, int localY, int localZ)
{
	const std::vector<Tree> biomeTrees = Biome::GetTrees(biomeType);
	uint32_t hash = Utils::HashInt((uint32_t)(localX * localZ), localY);

	switch (biomeType) {
	case BIOME_OCEAN:
		return biomeTrees[0].GetType(); // none

	case BIOME_BEACH:
		return biomeTrees[0].GetType(); // none

	case BIOME_TUNDRA:
		return biomeTrees[0].GetType(); // spruce

	case BIOME_TAIGA:
		return biomeTrees[0].GetType(); // spruce

	case BIOME_PLAINS:
		return biomeTrees[0].GetType(); // oak

	case BIOME_SWAMP:
		return biomeTrees[0].GetType(); // mangrove

	case BIOME_FOREST:
		if (d < 0.8f)
			return biomeTrees[0].GetType(); // oak
		else
			return biomeTrees[1].GetType(); // birch

	case BIOME_SHRUBLAND:
		if (d < 0.4f)
			return biomeTrees[0].GetType(); // oak
		else
			return biomeTrees[1].GetType(); // cherry

	case BIOME_DESERT:
		return biomeTrees[0].GetType(); // cactus

	case BIOME_RAINFOREST:
		if (d < 0.2f)
			return biomeTrees[0].GetType(); // oak
		else
			return biomeTrees[1].GetType(); // jungle

	case BIOME_SEASONFOREST:
		if (d < 0.6f)
			return biomeTrees[0].GetType(); // oak
		else
			return biomeTrees[1].GetType(); // birch

	case BIOME_SAVANA:
		if (d < 0.3f)
			return biomeTrees[0].GetType(); // oak
		else
			return biomeTrees[1].GetType(); // acacia

	case BIOME_SNOWY_TAIGA:
		return biomeTrees[0].GetType(); // spruce
	}

	return TREE_TYPE::TREE_NONE;
}