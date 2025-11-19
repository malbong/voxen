#pragma once

#include "Enums.h"
#include "Block.h"

#include <stdint.h>
#include <vector>

class TreeTypeInfoSet;

class Tree {
public:
	static const uint32_t TREE_TYPE_COUNT = 256;

	static BLOCK_TYPE GetTrunkBlockType(TREE_TYPE type);
	static BLOCK_TYPE GetLeafBlockType(TREE_TYPE type);
	static const std::vector<uint8_t> GetShape(TREE_TYPE type);
	static TREE_TYPE GetTreeTypeForBiome(
		BIOME_TYPE biomeType, float d, int localX, int localY, int localZ);

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
		  m_shape()
	{
	}
	TreeTypeInfo(
		BLOCK_TYPE trunkBlockType, BLOCK_TYPE leafBlockType, const std::vector<uint8_t>& shape)
		: m_trunkBlockType(trunkBlockType), m_leafBlockType(leafBlockType), m_shape(shape)
	{
	}
	~TreeTypeInfo() {}

	inline BLOCK_TYPE GetTrunkBlockType() const { return m_trunkBlockType; }
	inline void SetTrunkBlockType(BLOCK_TYPE trunkBlockType) { m_trunkBlockType = trunkBlockType; }

	inline BLOCK_TYPE GetLeafBlockType() const { return m_leafBlockType; }
	inline void SetLeafBlockType(BLOCK_TYPE leafBlockType) { m_leafBlockType = leafBlockType; }

	inline const std::vector<uint8_t>& GetShape() const { return m_shape; }
	inline void SetShape(std::vector<uint8_t>&& shape) { m_shape = std::move(shape); }


private:
	BLOCK_TYPE m_trunkBlockType;
	BLOCK_TYPE m_leafBlockType;
	std::vector<uint8_t> m_shape;
};


class TreeTypeInfoSet {
public:
	TreeTypeInfoSet() : m_treeTypeInfoSet(Tree::TREE_TYPE_COUNT)
	{
		m_treeTypeInfoSet[TREE_TYPE::TREE_OAK_LOG].SetTrunkBlockType(BLOCK_TYPE::BLOCK_OAK_LOG);
		m_treeTypeInfoSet[TREE_TYPE::TREE_OAK_LOG].SetLeafBlockType(BLOCK_TYPE::BLOCK_OAK_LEAF);

		m_treeTypeInfoSet[TREE_TYPE::TREE_SPRUCE_LOG].SetTrunkBlockType(
			BLOCK_TYPE::BLOCK_SPRUCE_LOG);
		m_treeTypeInfoSet[TREE_TYPE::TREE_SPRUCE_LOG].SetLeafBlockType(
			BLOCK_TYPE::BLOCK_SPRUCE_LEAF);

		m_treeTypeInfoSet[TREE_TYPE::TREE_MANGROVE_LOG].SetTrunkBlockType(
			BLOCK_TYPE::BLOCK_MANGROVE_LOG);
		m_treeTypeInfoSet[TREE_TYPE::TREE_MANGROVE_LOG].SetLeafBlockType(
			BLOCK_TYPE::BLOCK_MANGROVE_LEAF);

		m_treeTypeInfoSet[TREE_TYPE::TREE_BIRCH_LOG].SetTrunkBlockType(BLOCK_TYPE::BLOCK_BIRCH_LOG);
		m_treeTypeInfoSet[TREE_TYPE::TREE_BIRCH_LOG].SetLeafBlockType(BLOCK_TYPE::BLOCK_BIRCH_LEAF);

		m_treeTypeInfoSet[TREE_TYPE::TREE_CHERRY_LOG].SetTrunkBlockType(
			BLOCK_TYPE::BLOCK_CHERRY_LOG);
		m_treeTypeInfoSet[TREE_TYPE::TREE_CHERRY_LOG].SetLeafBlockType(
			BLOCK_TYPE::BLOCK_CHERRY_LEAF);

		m_treeTypeInfoSet[TREE_TYPE::TREE_CACTUS].SetTrunkBlockType(BLOCK_TYPE::BLOCK_CACTUS);
		m_treeTypeInfoSet[TREE_TYPE::TREE_CACTUS].SetLeafBlockType(BLOCK_TYPE::BLOCK_CACTUS);

		m_treeTypeInfoSet[TREE_TYPE::TREE_JUNGLE_LOG].SetTrunkBlockType(
			BLOCK_TYPE::BLOCK_JUNGLE_LOG);
		m_treeTypeInfoSet[TREE_TYPE::TREE_JUNGLE_LOG].SetLeafBlockType(
			BLOCK_TYPE::BLOCK_JUNGLE_LEAF);

		m_treeTypeInfoSet[TREE_TYPE::TREE_ACACIA_LOG].SetTrunkBlockType(
			BLOCK_TYPE::BLOCK_ACACIA_LOG);
		m_treeTypeInfoSet[TREE_TYPE::TREE_ACACIA_LOG].SetLeafBlockType(
			BLOCK_TYPE::BLOCK_ACACIA_LEAF);
	}

	inline const TreeTypeInfo& GetInfo(TREE_TYPE type) const { return m_treeTypeInfoSet[type]; }

private:
	std::vector<TreeTypeInfo> m_treeTypeInfoSet;
};