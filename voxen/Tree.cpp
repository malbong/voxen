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

const TreeShapeParams& Tree::GetTreeShapeParams(TREE_TYPE type)
{
	return m_treeTypeInfoSet.GetInfo(type).GetShapeParams();
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
		if (d < 0.5f)
			return biomeTrees[0].GetType(); // oak
		else
			return biomeTrees[1].GetType(); // mangrove

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

void AddLeafCluster(Vector3 center, Vector3 shrink, int radius, int carveStartY, int carveEndY,
	float carveScale, const PosInt3 worldPos, TreeShape& tree)
{
	for (int y = -radius; y <= radius; ++y) {
		float sy = y * shrink.y;

		for (int z = -radius; z <= radius; ++z) {
			float sz = z * shrink.z;

			for (int x = -radius; x <= radius; ++x) {
				float sx = x * shrink.x;

				if (carveStartY <= y && y <= carveEndY) {
					int wx = (int)(std::get<0>(worldPos) + center.x + x);
					int wy = (int)(std::get<1>(worldPos) + center.y + y + 1);
					int wz = (int)(std::get<2>(worldPos) + center.z + z);

					uint32_t hx = Utils::HashInt(wx, wx * wy * wz);
					uint32_t hz = Utils::HashInt(wz, wx * wy * wz);

					sx *= (1.0f + (carveScale * (hx % 2)));
					sz *= (1.0f + (carveScale * (hz % 2)));
				}

				if (sx * sx + sy * sy + sz * sz < radius * radius) {
					int nx = (int)center.x + x;
					int ny = (int)center.y + y;
					int nz = (int)center.z + z;

					if (nx < 0 || nx >= Tree::TREE_SIZE || nx < 0 || nx >= Tree::TREE_SIZE ||
						nx < 0 || nx >= Tree::TREE_SIZE)
						continue;

					if (tree[ny][nz][nx] == 0)
						tree[ny][nz][nx] = TREE_BLOCK_INDEX::LEAF;
				}
			}
		}
	}
}

void GenerateMangrove(const TreeShapeParams& params, TreeShape& tree) {}

void GenerateCherry(const TreeShapeParams& params, TreeShape& tree) {}

void GenerateCactus(const TreeShapeParams& params, TreeShape& tree) {}

void GenerateJungle(const TreeShapeParams& params, TreeShape& tree) {}

void GenerateAcacia(const TreeShapeParams& params, TreeShape& tree) {}

void GenerateSpruce(const TreeShapeParams& params, const PosInt3& worldPos, TreeShape& tree)
{
	// tree x, z at center
	int tx = Tree::TREE_SIZE / 2;
	int tz = Tree::TREE_SIZE / 2;

	// set trunk block
	int heightRange = Utils::RandomRangeByPos(worldPos, -2, 4);
	for (int y = 0; y < params.baseHeight + heightRange; ++y)
		tree[y][tz][tx] = TREE_BLOCK_INDEX::TRUNK;

	Vector3 center = Vector3((float)tx, (float)(params.baseHeight - 4 + heightRange), (float)tz);
	Vector3 shrink = Vector3(1.0f, 2.5f, 1.0f);
	int radiusRange = Utils::RandomRangeByPos(worldPos, -1, 1);
	int leafRadius = params.leafRadius + radiusRange;
	int carveStartY = -(1 + radiusRange);
	int carveEndY = radiusRange;
	float carveScale = 0.25f;
	AddLeafCluster(
		center, shrink, leafRadius, carveStartY, carveEndY, carveScale, worldPos, tree);

	// cap leaves
	center = Vector3((float)tx, (float)(params.baseHeight - 1 + heightRange), (float)tz);
	shrink = Vector3(1.0f, 1.5f, 1.0f);
	AddLeafCluster(center, shrink, 2, -1, 1, 0.25f, worldPos, tree);
}

void GenerateBasicTree(const TreeShapeParams& params, const PosInt3& worldPos, TreeShape& tree)
{
	// tree x, z at center
	int tx = Tree::TREE_SIZE / 2;
	int tz = Tree::TREE_SIZE / 2;

	// set trunk block
	int heightRange = Utils::RandomRangeByPos(worldPos, -1, 1);
	for (int y = 0; y < params.baseHeight + heightRange; ++y)
		tree[y][tz][tx] = TREE_BLOCK_INDEX::TRUNK;

	Vector3 center = Vector3((float)tx, (float)(params.baseHeight - 2 + heightRange), (float)tz);
	Vector3 shrink = Vector3(1.0f, 2.0f, 1.0f);
	int radiusRange = Utils::RandomRangeByPos(worldPos, 0, 1);
	int leafRadius = params.leafRadius + radiusRange;
	int carveStartY = -(1 + radiusRange);
	int carveEndY = radiusRange;
	float carveScale = 0.125f;
	AddLeafCluster(
		center, shrink, leafRadius, carveStartY, carveEndY, carveScale, worldPos, tree);

	// cap leaves
	center = Vector3((float)tx, (float)(params.baseHeight - 1 + heightRange), (float)tz);
	shrink = Vector3(1.0f, 1.5f, 1.0f);
	AddLeafCluster(center, shrink, 2, 0, 0, 0.25f, worldPos, tree);
}

void Tree::GenerateTreeShape(TREE_TYPE type, const PosInt3& worldPosInt3, TreeShape& outTreeShape)
{
	const TreeShapeParams& params = GetTreeShapeParams(type);
	return GenerateSpruce(params, worldPosInt3, outTreeShape);

	switch (type) {
	case TREE_TYPE::TREE_MANGROVE_LOG:
		return GenerateMangrove(params, outTreeShape);

	case TREE_TYPE::TREE_CHERRY_LOG:
		return GenerateCherry(params, outTreeShape);

	case TREE_TYPE::TREE_CACTUS:
		return GenerateCactus(params, outTreeShape);

	case TREE_TYPE::TREE_JUNGLE_LOG:
		return GenerateJungle(params, outTreeShape);

	case TREE_TYPE::TREE_ACACIA_LOG:
		return GenerateAcacia(params, outTreeShape);

	case TREE_TYPE::TREE_SPRUCE_LOG:
		return GenerateSpruce(params, worldPosInt3, outTreeShape);

	default: // oak, birch
		return GenerateBasicTree(params, worldPosInt3, outTreeShape);
	}
}