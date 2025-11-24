#pragma once

#include <cstdint>
#include <vector>

#include "Structure.h"

class BlockTypeInfoSet;

class Block {
public:
	static const uint32_t BLOCK_TYPE_COUNT = 256;

	static bool IsTransparency(BLOCK_TYPE type);
	static bool IsOpaque(BLOCK_TYPE type);
	static bool IsSemiAlpha(BLOCK_TYPE type);
	static uint8_t GetBlockTextureIndex(BLOCK_TYPE type, uint8_t face);
	static BLOCK_TYPE GetBlockTypeForInner(int x, int y, int z, float distribution);
	static BLOCK_TYPE GetBlockTypeForBiome(BIOME_TYPE biomeType, int y, float h, float d);
	static BLOCK_TYPE GetBlockType(int x, int y, int z, float continentalness, float erosion,
		float peaksValley, float temperature, float humidity, float distribution, float elevation);

	Block() : m_type(BLOCK_TYPE::BLOCK_NONE) {}
	Block(BLOCK_TYPE type) : m_type(type) {}
	~Block() {}

	inline BLOCK_TYPE GetType() const { return m_type; }
	inline void SetType(BLOCK_TYPE type) { m_type = type; }


private:
	static BlockTypeInfoSet m_blockTypeInfoSet;

	BLOCK_TYPE m_type;
};


class BlockTypeInfo {

public:
	BlockTypeInfo()
		: m_texTopIndex(0), m_texSideIndex(0), m_texBottomIndex(0), m_isTransparency(true),
		  m_isOpaque(false), m_isSemiAlpha(false)

	{
	}
	~BlockTypeInfo() {}

	inline void Init(uint8_t texTopIndex, uint8_t texSideIndex, uint8_t texBottomIndex,
		bool isTransparency, bool isOpaque, bool isSemiAlpha)
	{
		m_texTopIndex = texTopIndex;
		m_texSideIndex = texSideIndex;
		m_texBottomIndex = texBottomIndex;
		m_isTransparency = isTransparency;
		m_isOpaque = isOpaque;
		m_isSemiAlpha = isSemiAlpha;
	}

	inline uint8_t GetTexIndex(uint8_t face) const
	{
		if (face == DIR::TOP) {
			return m_texTopIndex;
		}
		else if (face == DIR::BOTTOM) {
			return m_texBottomIndex;
		}
		else {
			return m_texSideIndex;
		}
	}
	inline bool IsTransparency() const { return m_isTransparency; }
	inline bool IsOpaque() const { return m_isOpaque; }
	inline bool IsSemiAlpha() const { return m_isSemiAlpha; }


private:
	uint8_t m_texTopIndex;
	uint8_t m_texSideIndex;
	uint8_t m_texBottomIndex;
	bool m_isTransparency;
	bool m_isOpaque;
	bool m_isSemiAlpha;
};


class BlockTypeInfoSet {

public:
	BlockTypeInfoSet() : m_blockTypeInfoSet(Block::BLOCK_TYPE_COUNT)
	{
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_AIR].Init(0, 0, 0, true, false, false);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_WATER].Init(0, 0, 0, true, false, false);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_BEDROCK].Init(5, 5, 5, false, true, false);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_GRASS].Init(1, 2, 3, false, true, false);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_SNOW_GRASS].Init(7, 8, 3, false, true, false);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_DIRT].Init(3, 3, 3, false, true, false);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_STONE].Init(6, 6, 6, false, true, false);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_SAND].Init(4, 4, 4, false, true, false);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_SNOW].Init(9, 9, 9, false, true, false);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_GRAVEL].Init(10, 10, 10, false, true, false);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_SANDSTONE].Init(13, 11, 12, false, true, false);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_CLAY].Init(14, 14, 14, false, true, false);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_ANDESITE].Init(15, 15, 15, false, true, false);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_COAL_ORE].Init(21, 21, 21, false, true, false);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_GOLD_ORE].Init(16, 16, 16, false, true, false);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_REDSTONE_ORE].Init(17, 17, 17, false, true, false);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_DIAMOND_ORE].Init(18, 18, 18, false, true, false);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_COPPER_ORE].Init(19, 19, 19, false, true, false);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_IRON_ORE].Init(20, 20, 20, false, true, false);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_COARSE].Init(22, 22, 22, false, true, false);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_PODZOL].Init(23, 24, 3, false, true, false);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_ICE].Init(25, 25, 25, false, true, false);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_GOLD].Init(26, 26, 26, false, true, false);
		
		// tree
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_OAK_LOG].Init(28, 27, 28, false, true, false);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_SPRUCE_LOG].Init(30, 29, 30, false, true, false);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_MANGROVE_LOG].Init(32, 31, 32, false, true, false);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_BIRCH_LOG].Init(34, 33, 34, false, true, false);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_CHERRY_LOG].Init(36, 35, 36, false, true, false);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_CACTUS].Init(37, 38, 39, false, true, false);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_JUNGLE_LOG].Init(41, 40, 41, false, true, false);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_ACACIA_LOG].Init(43, 42, 43, false, true, false);

		// leaves
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_OAK_LEAF].Init(64, 64, 64, false, false, true);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_SPRUCE_LEAF].Init(65, 65, 65, false, false, true);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_MANGROVE_LEAF].Init(66, 66, 66, false, false, true);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_BIRCH_LEAF].Init(67, 67, 67, false, false, true);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_CHERRY_LEAF].Init(80, 80, 80, false, false, true);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_JUNGLE_LEAF].Init(68, 68, 68, false, false, true);
		m_blockTypeInfoSet[BLOCK_TYPE::BLOCK_ACACIA_LEAF].Init(69, 69, 69, false, false, true);

	}

	inline const BlockTypeInfo& GetInfo(BLOCK_TYPE type) const { return m_blockTypeInfoSet[type]; }

private:
	std::vector<BlockTypeInfo> m_blockTypeInfoSet;
};