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
	const std::vector<TREE_TYPE> biomeTrees = Biome::GetTrees(biomeType);
	uint32_t hash = Utils::HashInt((uint32_t)(localX * localZ), localY);

	switch (biomeType) {
	case BIOME_OCEAN:
		return biomeTrees[0]; // none

	case BIOME_BEACH:
		return biomeTrees[0]; // none

	case BIOME_TUNDRA:
		return biomeTrees[0]; // spruce

	case BIOME_TAIGA:
		return biomeTrees[0]; // spruce

	case BIOME_PLAINS:
		return biomeTrees[0]; // oak

	case BIOME_SWAMP:
		if (d < 0.5f)
			return biomeTrees[0]; // oak
		else
			return biomeTrees[1]; // mangrove

	case BIOME_FOREST:
		if (d < 0.8f)
			return biomeTrees[0]; // oak
		else
			return biomeTrees[1]; // birch

	case BIOME_SHRUBLAND:
		if (d < 0.4f)
			return biomeTrees[0]; // oak
		else
			return biomeTrees[1]; // cherry

	case BIOME_DESERT:
		return biomeTrees[0]; // cactus

	case BIOME_RAINFOREST:
		if (d < 0.2f)
			return biomeTrees[0]; // oak
		else
			return biomeTrees[1]; // jungle

	case BIOME_SEASONFOREST:
		if (d < 0.6f)
			return biomeTrees[0]; // oak
		else
			return biomeTrees[1]; // birch

	case BIOME_SAVANA:
		if (d < 0.3f)
			return biomeTrees[0]; // oak
		else
			return biomeTrees[1]; // acacia

	case BIOME_SNOWY_TAIGA:
		return biomeTrees[0]; // spruce
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

					uint32_t hx = Utils::HashInt(wx, 100043u);
					uint32_t hz = Utils::HashInt(wz, 400021u);

					sx *= (1.0f + (carveScale * (hx % 2)));
					sz *= (1.0f + (carveScale * (hz % 2)));
				}

				if (sx * sx + sy * sy + sz * sz < radius * radius) {
					int nx = (int)center.x + x;
					int ny = (int)center.y + y;
					int nz = (int)center.z + z;

					if (nx < 0 || nx >= Tree::TREE_SIZE || ny < 0 || ny >= Tree::TREE_SIZE ||
						nz < 0 || nz >= Tree::TREE_SIZE)
						continue;

					if (tree[ny][nz][nx] == TREE_BLOCK_INDEX::EMPTY)
						tree[ny][nz][nx] = TREE_BLOCK_INDEX::LEAF;
				}
			}
		}
	}
}

Vector3 GenerateRandomBasisDirection2D(const PosInt3 worldPos, int loop)
{
	Vector3 dir[4] = { { -1, 0, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };

	int rangeIndex = Utils::RandomRangeByPosForLoop(worldPos, loop, 54377u, 0, 3);

	return dir[rangeIndex];
}

Vector3 GenerateNextDirectionForRandom(const PosInt3& worldPos, Vector3 curDir, Vector3 prevDir,
	Vector3 initDir, int loop, int startDirY)
{
	Vector3 curReverseDir = -curDir;
	Vector3 prevReverseDir = -prevDir;
	Vector3 initReverseDir = -initDir;

	const int DIR_COUNT = 6;
	Vector3 dir[DIR_COUNT] = { { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, 1 },
		{ 0, 0, -1 } };

	std::vector<Vector3> possibleDirs;
	for (int i = 0; i < DIR_COUNT; ++i) {
		if (dir[i] == curReverseDir || dir[i] == prevReverseDir || dir[i] == initReverseDir)
			continue;

		if (i == DIR::BOTTOM || i == DIR::TOP) {
			if (loop >= startDirY) {
				possibleDirs.push_back(dir[i]);
			}
		}
		else {
			possibleDirs.push_back(dir[i]);
		}
	}

	int chooseIndex =
		Utils::RandomRangeByPosForLoop(worldPos, loop, 0, (int)possibleDirs.size() - 1);

	return possibleDirs[chooseIndex];
}

Vector3 GenerateNextDirectionForGradient(Vector3 diffPos, Vector3 gradient)
{
	const int DIR_COUNT = 6;
	Vector3 dir[DIR_COUNT] = { { -1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, -1.0f, 0.0f },
		{ 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } };

	gradient.Normalize();

	Vector3 bestDir(0.0f);
	float bestScalar = -1.0f;

	for (int i = 0; i < DIR_COUNT; ++i) {
		Vector3 nextDiffPos = diffPos + dir[i];
		if (nextDiffPos.Length() == 0.0f)
			continue;

		if (diffPos.Length() >= nextDiffPos.Length())
			continue;

		nextDiffPos.Normalize();

		float scalar = gradient.Dot(nextDiffPos);
		if (scalar > bestScalar) {
			bestDir = dir[i];
			bestScalar = scalar;
		}
	}

	return bestDir;
}

void AddBranchForGradient(
	const PosInt3& worldPos, int branchLength, int startHeight, Vector3 gradient, TreeShape& tree)
{
	int nx = Tree::TREE_SIZE / 2;
	int nz = Tree::TREE_SIZE / 2;
	int ny = startHeight;

	Vector3 diffPos = Vector3(0.0f);
	Vector3 curDir = GenerateNextDirectionForGradient(diffPos, gradient);

	for (int j = 0; j < branchLength; ++j) {
		nx += (int)curDir.x;
		ny += (int)curDir.y;
		nz += (int)curDir.z;

		if (nx < 0 || nx >= Tree::TREE_SIZE || ny < 0 || ny >= Tree::TREE_SIZE || nz < 0 ||
			nz >= Tree::TREE_SIZE)
			continue;

		if (tree[ny][nz][nx] == TREE_BLOCK_INDEX::EMPTY)
			tree[ny][nz][nx] = TREE_BLOCK_INDEX::TRUNK;

		diffPos += curDir;
		curDir = GenerateNextDirectionForGradient(diffPos, gradient);
	}
}

void AddBranchForRandom(const PosInt3& worldPos, int branchLength, int startHeight, int loop,
	int startYDir, TreeShape& tree)
{
	int nx = Tree::TREE_SIZE / 2;
	int nz = Tree::TREE_SIZE / 2;
	int ny = startHeight;

	Vector3 initDir = GenerateRandomBasisDirection2D(worldPos, loop); 
	Vector3 curDir = initDir;
	Vector3 prevDir = Vector3(0.0f);

	for (int j = 0; j < branchLength; ++j) {
		nx += (int)curDir.x;
		ny += (int)curDir.y;
		nz += (int)curDir.z;

		if (nx < 0 || nx >= Tree::TREE_SIZE || ny < 0 || ny >= Tree::TREE_SIZE || nz < 0 ||
			nz >= Tree::TREE_SIZE)
			continue;

		if (tree[ny][nz][nx] == TREE_BLOCK_INDEX::EMPTY)
			tree[ny][nz][nx] = TREE_BLOCK_INDEX::TRUNK;

		Vector3 tmpDir = curDir;
		curDir = GenerateNextDirectionForRandom(worldPos, curDir, prevDir, initDir, j, startYDir);
		prevDir = tmpDir;
	}
}

void GenerateMangrove(const TreeShapeParams& params, TreeShape& tree) {}

void GenerateJungle(const TreeShapeParams& params, TreeShape& tree) {}

void GenerateCactus(const TreeShapeParams& params, const PosInt3& worldPos, TreeShape& tree)
{
	int tx = Tree::TREE_SIZE / 2;
	int tz = Tree::TREE_SIZE / 2;

	int heightRange = params.baseHeight + Utils::RandomRangeByPos(worldPos, -1, 1);
	for (int y = 0; y < heightRange; ++y) {
		tree[y][tz][tx] = TREE_BLOCK_INDEX::TRUNK;
	}

	for (int i = 0; i < params.branchCount; ++i) {
		int branchLengthRange =
			params.branchLength + Utils::RandomRangeByPosForLoop(worldPos, i, 95369u, -1, 1);
		int branchStartHeightRange =
			params.branchStartHeight + Utils::RandomRangeByPosForLoop(worldPos, i, 65599u, -1, 0);

		AddBranchForRandom(worldPos, branchLengthRange, branchStartHeightRange, i, 2, tree);
	}
}

void GenerateAcacia(const TreeShapeParams& params, TreeShape& tree) {}

void GenerateCherry(const TreeShapeParams& params, TreeShape& tree) {}

void GenerateSpruce(const TreeShapeParams& params, const PosInt3& worldPos, TreeShape& tree)
{
	// tree x, z at center
	int tx = Tree::TREE_SIZE / 2;
	int tz = Tree::TREE_SIZE / 2;

	// set trunk block
	int heightRange = params.baseHeight + Utils::RandomRangeByPos(worldPos, -2, 4);
	for (int y = 0; y < heightRange; ++y)
		tree[y][tz][tx] = TREE_BLOCK_INDEX::TRUNK;

	Vector3 center = Vector3((float)tx, (float)(heightRange - 4), (float)tz);
	Vector3 shrink = Vector3(1.0f, 2.5f, 1.0f);
	int radiusRange = Utils::RandomRangeByPos(worldPos, -1, 1);
	int leafRadius = params.leafRadius + radiusRange;
	int carveStartY = -(1 + radiusRange);
	int carveEndY = radiusRange;
	float carveScale = 0.25f;
	AddLeafCluster(center, shrink, leafRadius, carveStartY, carveEndY, carveScale, worldPos, tree);

	// cap leaves
	center = Vector3((float)tx, (float)(heightRange - 1), (float)tz);
	shrink = Vector3(1.0f, 1.5f, 1.0f);
	AddLeafCluster(center, shrink, 2, -1, 1, 0.25f, worldPos, tree);
}

void GenerateBasicTree(const TreeShapeParams& params, const PosInt3& worldPos, TreeShape& tree)
{
	// tree x, z at center
	int tx = Tree::TREE_SIZE / 2;
	int tz = Tree::TREE_SIZE / 2;

	// set trunk block
	int heightRange = params.baseHeight + Utils::RandomRangeByPos(worldPos, -1, 1);
	for (int y = 0; y < heightRange; ++y)
		tree[y][tz][tx] = TREE_BLOCK_INDEX::TRUNK;

	Vector3 center = Vector3((float)tx, (float)(heightRange - 2), (float)tz);
	Vector3 shrink = Vector3(1.0f, 2.0f, 1.0f);
	int radiusRange = Utils::RandomRangeByPos(worldPos, 0, 1);
	int leafRadius = params.leafRadius + radiusRange;
	int carveStartY = -(1 + radiusRange);
	int carveEndY = radiusRange;
	float carveScale = 0.125f;
	AddLeafCluster(center, shrink, leafRadius, carveStartY, carveEndY, carveScale, worldPos, tree);

	// cap leaves
	center = Vector3((float)tx, (float)(heightRange - 1), (float)tz);
	shrink = Vector3(1.0f, 1.5f, 1.0f);
	AddLeafCluster(center, shrink, 2, 0, 0, 0.25f, worldPos, tree);
}

void Tree::GenerateTreeShape(TREE_TYPE type, const PosInt3& worldPosInt3, TreeShape& outTreeShape)
{
	const TreeShapeParams& params = GetTreeShapeParams(type);

	switch (type) {
	case TREE_TYPE::TREE_MANGROVE_LOG:
		return GenerateMangrove(params, outTreeShape);

	case TREE_TYPE::TREE_CHERRY_LOG:
		return GenerateCherry(params, outTreeShape);

	case TREE_TYPE::TREE_CACTUS:
		return GenerateCactus(params, worldPosInt3, outTreeShape);

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