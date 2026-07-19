#pragma once

#include "Enums.h"
#include "Structure.h"
#include "Block.h"
#include "Pos.h"

#include <stdint.h>
#include <vector>

struct TreeShapeParams {
	int baseHeight;
	int branchCount;
	int branchLength;
	int branchStartHeight;
	int leafRadius;
};


using TreeShape = uint8_t[25][25][25];

class TreeTypeInfoSet;
class Tree {
public:
	static const uint32_t TREE_TYPE_COUNT = 256;
	static const uint8_t TREE_SIZE = 25;

	static BLOCK_TYPE GetTrunkBlockType(TREE_TYPE type);
	static BLOCK_TYPE GetLeafBlockType(TREE_TYPE type);
	static const TreeShapeParams& GetTreeShapeParams(TREE_TYPE type);

	static TREE_TYPE GetTreeTypeForBiome(
		BIOME_TYPE biomeType, float d, int localX, int localY, int localZ);
	static void GenerateTreeShape(TREE_TYPE type, const PosInt3& worldPos, TreeShape& outTree);

	Tree() : m_type(TREE_TYPE::TREE_OAK_LOG) {}
	Tree(TREE_TYPE type) : m_type(type) {}
	~Tree() {}

	inline TREE_TYPE GetType() const { return m_type; }
	inline void SetType(TREE_TYPE type) { m_type = type; }

private:
	static TreeTypeInfoSet m_treeTypeInfoSet;

	TREE_TYPE m_type;
};

class TreeTypeInfo {
public:
	TreeTypeInfo()
		: m_trunkBlockType(BLOCK_TYPE::BLOCK_OAK_LOG), m_leafBlockType(BLOCK_TYPE::BLOCK_OAK_LEAF),
		  m_shapeParams()
	{
	}
	TreeTypeInfo(BLOCK_TYPE trunkBlockType, BLOCK_TYPE leafBlockType, TreeShapeParams shapeParams)
		: m_trunkBlockType(trunkBlockType), m_leafBlockType(leafBlockType),
		  m_shapeParams(shapeParams)
	{
	}
	~TreeTypeInfo() {}

	inline BLOCK_TYPE GetTrunkBlockType() const { return m_trunkBlockType; }
	inline void SetTrunkBlockType(BLOCK_TYPE trunkBlockType) { m_trunkBlockType = trunkBlockType; }

	inline BLOCK_TYPE GetLeafBlockType() const { return m_leafBlockType; }
	inline void SetLeafBlockType(BLOCK_TYPE leafBlockType) { m_leafBlockType = leafBlockType; }

	inline const TreeShapeParams& GetShapeParams() const { return m_shapeParams; }
	inline void SetShapeParams(TreeShapeParams shapeParams) { m_shapeParams = shapeParams; }

private:
	BLOCK_TYPE m_trunkBlockType;
	BLOCK_TYPE m_leafBlockType;
	TreeShapeParams m_shapeParams;
};


class TreeTypeInfoSet {
public:
	TreeTypeInfoSet() : m_treeTypeInfoSet(Tree::TREE_TYPE_COUNT)
	{
		m_treeTypeInfoSet[TREE_TYPE::TREE_OAK_LOG].SetTrunkBlockType(BLOCK_TYPE::BLOCK_OAK_LOG);
		m_treeTypeInfoSet[TREE_TYPE::TREE_OAK_LOG].SetLeafBlockType(BLOCK_TYPE::BLOCK_OAK_LEAF);
		m_treeTypeInfoSet[TREE_TYPE::TREE_OAK_LOG].SetShapeParams({ 7, 0, 0, 0, 3 });

		m_treeTypeInfoSet[TREE_TYPE::TREE_SPRUCE_LOG].SetTrunkBlockType(
			BLOCK_TYPE::BLOCK_SPRUCE_LOG);
		m_treeTypeInfoSet[TREE_TYPE::TREE_SPRUCE_LOG].SetLeafBlockType(
			BLOCK_TYPE::BLOCK_SPRUCE_LEAF);
		m_treeTypeInfoSet[TREE_TYPE::TREE_SPRUCE_LOG].SetShapeParams({ 12, 0, 0, 0, 4 });

		m_treeTypeInfoSet[TREE_TYPE::TREE_MANGROVE_LOG].SetTrunkBlockType(
			BLOCK_TYPE::BLOCK_MANGROVE_LOG);
		m_treeTypeInfoSet[TREE_TYPE::TREE_MANGROVE_LOG].SetLeafBlockType(
			BLOCK_TYPE::BLOCK_MANGROVE_LEAF);
		m_treeTypeInfoSet[TREE_TYPE::TREE_MANGROVE_LOG].SetShapeParams({ 7, 0, 0, 0, 3 });

		m_treeTypeInfoSet[TREE_TYPE::TREE_BIRCH_LOG].SetTrunkBlockType(BLOCK_TYPE::BLOCK_BIRCH_LOG);
		m_treeTypeInfoSet[TREE_TYPE::TREE_BIRCH_LOG].SetLeafBlockType(BLOCK_TYPE::BLOCK_BIRCH_LEAF);
		m_treeTypeInfoSet[TREE_TYPE::TREE_BIRCH_LOG].SetShapeParams({6, 0, 0, 0, 2});

		m_treeTypeInfoSet[TREE_TYPE::TREE_CHERRY_LOG].SetTrunkBlockType(
			BLOCK_TYPE::BLOCK_CHERRY_LOG);
		m_treeTypeInfoSet[TREE_TYPE::TREE_CHERRY_LOG].SetLeafBlockType(
			BLOCK_TYPE::BLOCK_CHERRY_LEAF);
		m_treeTypeInfoSet[TREE_TYPE::TREE_CHERRY_LOG].SetShapeParams({ 9, 2, 13, 3, 6});

		m_treeTypeInfoSet[TREE_TYPE::TREE_CACTUS].SetTrunkBlockType(BLOCK_TYPE::BLOCK_CACTUS);
		m_treeTypeInfoSet[TREE_TYPE::TREE_CACTUS].SetLeafBlockType(BLOCK_TYPE::BLOCK_CACTUS);
		m_treeTypeInfoSet[TREE_TYPE::TREE_CACTUS].SetShapeParams({7, 2, 4, 4, 0});

		m_treeTypeInfoSet[TREE_TYPE::TREE_JUNGLE_LOG].SetTrunkBlockType(
			BLOCK_TYPE::BLOCK_JUNGLE_LOG);
		m_treeTypeInfoSet[TREE_TYPE::TREE_JUNGLE_LOG].SetLeafBlockType(
			BLOCK_TYPE::BLOCK_JUNGLE_LEAF);
		m_treeTypeInfoSet[TREE_TYPE::TREE_JUNGLE_LOG].SetShapeParams({ 19, 3, 13, 13, 7});

		m_treeTypeInfoSet[TREE_TYPE::TREE_ACACIA_LOG].SetTrunkBlockType(
			BLOCK_TYPE::BLOCK_ACACIA_LOG);
		m_treeTypeInfoSet[TREE_TYPE::TREE_ACACIA_LOG].SetLeafBlockType(
			BLOCK_TYPE::BLOCK_ACACIA_LEAF);
		m_treeTypeInfoSet[TREE_TYPE::TREE_ACACIA_LOG].SetShapeParams({ 10, 1, 10, 3, 6 });
	}

	inline const TreeTypeInfo& GetInfo(TREE_TYPE type) const { return m_treeTypeInfoSet[type]; }

private:
	std::vector<TreeTypeInfo> m_treeTypeInfoSet;
};