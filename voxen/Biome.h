#pragma once

#include <cstdint>
#include <vector>

#include "Structure.h"
#include "Instance.h"
#include "Tree.h"

class BiomeTypeInfoSet;

class Biome {
public:
	static const uint32_t BIOME_TYPE_COUNT = 13;

	static RGBA_UINT GetBaseColor(BIOME_TYPE type);
	static uint32_t GetMaxInstanceCountPerChunk(BIOME_TYPE type);
	static uint32_t GetMaxTreeCountPerChunk(BIOME_TYPE type);
	static const std::vector<INSTANCE_TYPE>& GetInstances(BIOME_TYPE type);
	static const std::vector<TREE_TYPE>& GetTrees(BIOME_TYPE type);
	static BIOME_TYPE GetBiomeType(
		float elevation, float temperature, float humidity, float peaksValley, float erosion);

private:
	static BiomeTypeInfoSet m_biomeTypeInfoSet;
};


class BiomeTypeInfo {

public:
	BiomeTypeInfo()
		: m_baseColor(RGBA_UINT(0, 0, 0, 0)), m_maxTreeCountPerChunk(0),
		  m_maxInstanceCountPerChunk(0), m_instances(), m_trees()
	{
	}
	~BiomeTypeInfo() {}

	inline RGBA_UINT GetBaseColor() const { return m_baseColor; }
	inline void SetBaseColor(RGBA_UINT baseColor) { m_baseColor = baseColor; }

	inline uint32_t GetMaxTreeCountPerChunk() const { return m_maxTreeCountPerChunk; }
	inline void SetMaxTreeCountPerChunk(uint32_t maxTreeCountPerChunk)
	{
		m_maxTreeCountPerChunk = maxTreeCountPerChunk;
	}

	inline uint32_t GetMaxInstanceCountPerChunk() const { return m_maxInstanceCountPerChunk; }
	inline void SetMaxInstanceCountPerChunk(uint32_t maxInstanceCountPerChunk)
	{
		m_maxInstanceCountPerChunk = maxInstanceCountPerChunk;
	}

	inline const std::vector<INSTANCE_TYPE>& GetInstances() const { return m_instances; }
	inline void SetInstances(std::vector<INSTANCE_TYPE>&& instances)
	{
		m_instances = std::move(instances);
	}

	inline const std::vector<TREE_TYPE>& GetTrees() const { return m_trees; }
	inline void SetTrees(std::vector<TREE_TYPE>&& trees) { m_trees = std::move(trees); }

private:
	RGBA_UINT m_baseColor;
	uint32_t m_maxTreeCountPerChunk;
	uint32_t m_maxInstanceCountPerChunk;
	std::vector<INSTANCE_TYPE> m_instances;
	std::vector<TREE_TYPE> m_trees;
};


class BiomeTypeInfoSet {

public:
	BiomeTypeInfoSet() : m_biomeTypeInfoSet(Biome::BIOME_TYPE_COUNT)
	{
		std::vector<INSTANCE_TYPE> tmpInstances;
		std::vector<TREE_TYPE> tmpTrees;

		// BIOME_OCEAN
		// instance: seagrass, kelp
		// tree: none
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_OCEAN].SetBaseColor(RGBA_UINT(0, 0, 255, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_OCEAN].SetMaxInstanceCountPerChunk(64);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_OCEAN].SetMaxTreeCountPerChunk(0);
		tmpInstances.clear();
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_SEAGRASS);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_KELP);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_OCEAN].SetInstances(std::move(tmpInstances));
		tmpTrees.clear();
		tmpTrees.push_back(TREE_TYPE::TREE_NONE);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_OCEAN].SetTrees(std::move(tmpTrees));
		

		// BIOME_BEACH
		// instance: no instance
		// tree: none
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_BEACH].SetBaseColor(RGBA_UINT(255, 223, 128, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_BEACH].SetMaxInstanceCountPerChunk(0);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_BEACH].SetMaxTreeCountPerChunk(0);
		tmpInstances.clear();
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_NONE);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_BEACH].SetInstances(std::move(tmpInstances));
		tmpTrees.clear();
		tmpTrees.push_back(TREE_TYPE::TREE_NONE);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_BEACH].SetTrees(std::move(tmpTrees));


		// BIOME_TUNDRA
		// instance: no instance
		// tree: spruce
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_TUNDRA].SetBaseColor(RGBA_UINT(235, 235, 235, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_TUNDRA].SetMaxInstanceCountPerChunk(0);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_TUNDRA].SetMaxTreeCountPerChunk(3);
		tmpInstances.clear();
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_NONE);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_TUNDRA].SetInstances(std::move(tmpInstances));
		tmpTrees.clear();
		tmpTrees.push_back(TREE_TYPE::TREE_SPRUCE_LOG);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_TUNDRA].SetTrees(std::move(tmpTrees));


		// BIOME_TAIGA
		// instance: grass, fern, large fern, sweet berry bush
		// tree: spruce
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_TAIGA].SetBaseColor(RGBA_UINT(59, 94, 84, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_TAIGA].SetMaxInstanceCountPerChunk(64);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_TAIGA].SetMaxTreeCountPerChunk(8);
		tmpInstances.clear();
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_GRASS);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_FERN);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_LARGE_FERN);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_SWEET_BERRY_BUSH);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_TAIGA].SetInstances(std::move(tmpInstances));
		tmpTrees.clear();
		tmpTrees.push_back(TREE_TYPE::TREE_SPRUCE_LOG);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_TAIGA].SetTrees(std::move(tmpTrees));


		// BIOME_PLAINS
		// instance: grass, oxeye daisy, cornflower, tulips
		// tree: oak
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_PLAINS].SetBaseColor(RGBA_UINT(128, 169, 91, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_PLAINS].SetMaxInstanceCountPerChunk(96);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_PLAINS].SetMaxTreeCountPerChunk(12);
		tmpInstances.clear();
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_GRASS);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_OXEYE_DAISY);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_CORN_FLOWER);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_TULIP_PINK);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_TULIP_RED);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_TULIP_WHITE);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_TULIP_ORANGE);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_PLAINS].SetInstances(std::move(tmpInstances));
		tmpTrees.clear();
		tmpTrees.push_back(TREE_TYPE::TREE_OAK_LOG);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_PLAINS].SetTrees(std::move(tmpTrees));


		// BIOME_SWAMP
		// instance: grass, seagrass, blue orchid, mushrooms, dead bush
		// tree: oak, mangrove
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SWAMP].SetBaseColor(RGBA_UINT(20, 249, 183, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SWAMP].SetMaxInstanceCountPerChunk(128);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SWAMP].SetMaxTreeCountPerChunk(16);
		tmpInstances.clear();
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_GRASS);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_SEAGRASS);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_BLUE_ORCHID);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_MUSHROOM_BROWN);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_MUSHROOM_RED);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_DEAD_BUSH);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SWAMP].SetInstances(std::move(tmpInstances));
		tmpTrees.clear();
		tmpTrees.push_back(TREE_TYPE::TREE_OAK_LOG);
		tmpTrees.push_back(TREE_TYPE::TREE_MANGROVE_LOG);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SWAMP].SetTrees(std::move(tmpTrees));


		// BIOME_FOREST
		// instance: grass, Rose, Lily of the Valley, Allium
		// tree: oak, birch
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_FOREST].SetBaseColor(RGBA_UINT(59, 123, 78, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_FOREST].SetMaxInstanceCountPerChunk(160);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_FOREST].SetMaxTreeCountPerChunk(32);
		tmpInstances.clear();
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_GRASS);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_ROSE_BLUE);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_ROSE_RED);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_ROSE_PLANTS);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_LILY_OF_THE_VALLEY);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_ALLIUM);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_FOREST].SetInstances(std::move(tmpInstances));
		tmpTrees.clear();
		tmpTrees.push_back(TREE_TYPE::TREE_OAK_LOG);
		tmpTrees.push_back(TREE_TYPE::TREE_BIRCH_LOG);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_FOREST].SetTrees(std::move(tmpTrees));


		// BIOME_SHRUBLAND
		// instance: grass, Dandelion, Cornflower, Allium, Oxeye Daisy, tulips
		// tree: oak, cherry
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SHRUBLAND].SetBaseColor(RGBA_UINT(163, 184, 99, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SHRUBLAND].SetMaxInstanceCountPerChunk(96);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SHRUBLAND].SetMaxTreeCountPerChunk(12);
		tmpInstances.clear();
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_GRASS);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_DANDELION);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_CORN_FLOWER);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_ALLIUM);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_OXEYE_DAISY);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_TULIP_PINK);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_TULIP_RED);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_TULIP_WHITE);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_TULIP_ORANGE);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SHRUBLAND].SetInstances(std::move(tmpInstances));
		tmpTrees.clear();
		tmpTrees.push_back(TREE_TYPE::TREE_OAK_LOG);
		tmpTrees.push_back(TREE_TYPE::TREE_CHERRY_LOG);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SHRUBLAND].SetTrees(std::move(tmpTrees));


		// BIOME_DESERT
		// instance: dead bush
		// tree: cactus
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_DESERT].SetBaseColor(RGBA_UINT(214, 131, 31, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_DESERT].SetMaxInstanceCountPerChunk(8);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_DESERT].SetMaxTreeCountPerChunk(2);
		tmpInstances.clear();
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_DEAD_BUSH);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_DESERT].SetInstances(std::move(tmpInstances));
		tmpTrees.clear();
		tmpTrees.push_back(TREE_TYPE::TREE_CACTUS);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_DESERT].SetTrees(std::move(tmpTrees));


		// BIOME_RAINFOREST
		// instance: grass, Fern, Large Fern
		// tree: oak, jungle
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_RAINFOREST].SetBaseColor(RGBA_UINT(93, 130, 21, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_RAINFOREST].SetMaxInstanceCountPerChunk(160);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_RAINFOREST].SetMaxTreeCountPerChunk(32);
		tmpInstances.clear();
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_GRASS);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_FERN);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_LARGE_FERN);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_RAINFOREST].SetInstances(std::move(tmpInstances));
		tmpTrees.clear();
		tmpTrees.push_back(TREE_TYPE::TREE_OAK_LOG);
		tmpTrees.push_back(TREE_TYPE::TREE_JUNGLE_LOG);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_RAINFOREST].SetTrees(std::move(tmpTrees));


		// BIOME_SEASONFOREST
		// instance: grass, Allium, Lily of the Valley, Rose Bush, Various Tulips
		// tree: oak, birch
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SEASONFOREST].SetBaseColor(
			RGBA_UINT(182, 219, 97, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SEASONFOREST].SetMaxInstanceCountPerChunk(160);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SEASONFOREST].SetMaxTreeCountPerChunk(32);
		tmpInstances.clear();
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_GRASS);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_ALLIUM);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_LILY_OF_THE_VALLEY);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_ROSE_PLANTS);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_TULIP_PINK);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_TULIP_RED);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_TULIP_WHITE);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_TULIP_ORANGE);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SEASONFOREST].SetInstances(std::move(tmpInstances));
		tmpTrees.clear();
		tmpTrees.push_back(TREE_TYPE::TREE_OAK_LOG);
		tmpTrees.push_back(TREE_TYPE::TREE_BIRCH_LOG);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SEASONFOREST].SetTrees(std::move(tmpTrees));


		// BIOME_SAVANA
		// instance: grass
		// tree: oak, acacia
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SAVANA].SetBaseColor(RGBA_UINT(182, 173, 97, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SAVANA].SetMaxInstanceCountPerChunk(64);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SAVANA].SetMaxTreeCountPerChunk(8);
		tmpInstances.clear();
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_GRASS);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SAVANA].SetInstances(std::move(tmpInstances));
		tmpTrees.clear();
		tmpTrees.push_back(TREE_TYPE::TREE_OAK_LOG);
		tmpTrees.push_back(TREE_TYPE::TREE_ACACIA_LOG);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SAVANA].SetTrees(std::move(tmpTrees));


		// BIOME_SNOWY_TAIGA
		// instance: grass, fern, large fern, sweet berry bush
		// tree: spruce
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SNOWY_TAIGA].SetBaseColor(
			RGBA_UINT(200, 255, 239, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SNOWY_TAIGA].SetMaxInstanceCountPerChunk(32);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SNOWY_TAIGA].SetMaxTreeCountPerChunk(4);
		tmpInstances.clear();
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_GRASS);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_FERN);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_LARGE_FERN);
		tmpInstances.push_back(INSTANCE_TYPE::INSTANCE_SWEET_BERRY_BUSH);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SNOWY_TAIGA].SetInstances(std::move(tmpInstances));
		tmpTrees.clear();
		tmpTrees.push_back(TREE_TYPE::TREE_SPRUCE_LOG);
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SNOWY_TAIGA].SetTrees(std::move(tmpTrees));
	}

	inline const BiomeTypeInfo& GetInfo(BIOME_TYPE type) const { return m_biomeTypeInfoSet[type]; }


private:
	std::vector<BiomeTypeInfo> m_biomeTypeInfoSet;
};