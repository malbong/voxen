#pragma once

#include <cstdint>
#include <vector>

#include "Structure.h"
#include "Instance.h"

class BiomeTypeInfoSet;

class Biome {
public:
	static const uint32_t BIOME_TYPE_COUNT = 13;

	static RGBA_UINT GetBaseColor(BIOME_TYPE type);
	static uint8_t GetMaxInstanceCountPerChunk(BIOME_TYPE type);
	static const std::vector<Instance>& GetInstances(BIOME_TYPE type);
	static BIOME_TYPE GetBiomeType(
		float elevation, float temperature, float humidity, float peaksValley, float erosion);

private:
	static BiomeTypeInfoSet m_biomeTypeInfoSet;
};


class BiomeTypeInfo {

public:
	BiomeTypeInfo() : m_baseColor(RGBA_UINT(0, 0, 0, 0)), m_maxInstanceCountPerChunk(0), m_instances()
	{
	}
	~BiomeTypeInfo() {}

	inline RGBA_UINT GetBaseColor() const { return m_baseColor; }
	inline uint8_t GetMaxInstanceCountPerChunk() const { return m_maxInstanceCountPerChunk; }
	inline const std::vector<Instance>& GetInstances() const { return m_instances; }

	inline void SetBaseColor(RGBA_UINT baseColor) { m_baseColor = baseColor; }
	inline void SetMaxInstanceCountPerChunk(uint8_t maxInstanceCountPerChunk)
	{
		m_maxInstanceCountPerChunk = maxInstanceCountPerChunk;
	}
	inline void SetInstances(std::vector<Instance>&& instances)
	{
		m_instances = std::move(instances);
	}

private:
	RGBA_UINT m_baseColor;
	uint8_t m_maxInstanceCountPerChunk;
	std::vector<Instance> m_instances;
};


class BiomeTypeInfoSet {

public:
	BiomeTypeInfoSet() : m_biomeTypeInfoSet(Biome::BIOME_TYPE_COUNT)
	{
		std::vector<Instance> tmpInstances;

		// BIOME_OCEAN
		// seagrass
		// kelp
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_OCEAN].SetBaseColor(RGBA_UINT(0, 0, 255, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_OCEAN].SetMaxInstanceCountPerChunk(125);
		tmpInstances.clear();
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_SEAGRASS));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_KELP));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_OCEAN].SetInstances(std::move(tmpInstances));
		

		// BIOME_BEACH
		// no instance
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_BEACH].SetBaseColor(RGBA_UINT(255, 223, 128, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_BEACH].SetMaxInstanceCountPerChunk(125);
		tmpInstances.clear();
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_NONE));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_BEACH].SetInstances(std::move(tmpInstances));


		// BIOME_TUNDRA
		// no instance
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_TUNDRA].SetBaseColor(RGBA_UINT(235, 235, 235, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_TUNDRA].SetMaxInstanceCountPerChunk(125);
		tmpInstances.clear();
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_NONE));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_TUNDRA].SetInstances(std::move(tmpInstances));


		// BIOME_TAIGA
		// grass
		// fern
		// large fern
		// sweet berry bush
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_TAIGA].SetBaseColor(RGBA_UINT(59, 94, 84, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_TAIGA].SetMaxInstanceCountPerChunk(125);
		tmpInstances.clear();
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_SHORT_GRASS));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_FERN));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_LARGE_FERN));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_SWEET_BERRY_BUSH));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_TAIGA].SetInstances(std::move(tmpInstances));


		// BIOME_PLAINS
		// grass
		// oxeye daisy
		// cornflower
		// tulips
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_PLAINS].SetBaseColor(RGBA_UINT(128, 169, 91, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_PLAINS].SetMaxInstanceCountPerChunk(125);
		tmpInstances.clear();
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_SHORT_GRASS));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_OXEYE_DAISY));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_CORN_FLOWER));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_TULIP_PINK));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_TULIP_RED));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_TULIP_WHITE));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_TULIP_ORANGE));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_PLAINS].SetInstances(std::move(tmpInstances));


		// BIOME_SWAMP
		// grass
		// seagrass
		// blue orchid
		// mushrooms
		// dead bush
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SWAMP].SetBaseColor(RGBA_UINT(20, 249, 183, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SWAMP].SetMaxInstanceCountPerChunk(125);
		tmpInstances.clear();
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_SHORT_GRASS));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_SEAGRASS));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_BLUE_ORCHID));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_MUSHROOM_BROWN));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_MUSHROOM_RED));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_DEAD_BUSH));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SWAMP].SetInstances(std::move(tmpInstances));


		// BIOME_FOREST
		// grass
		// Rose 
		// Lily of the Valley 
		// Allium
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_FOREST].SetBaseColor(RGBA_UINT(59, 123, 78, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_FOREST].SetMaxInstanceCountPerChunk(125);
		tmpInstances.clear();
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_SHORT_GRASS));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_ROSE_BLUE));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_ROSE_RED));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_ROSE_PLANTS));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_LILY_OF_THE_VALLEY));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_ALLIUM));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_FOREST].SetInstances(std::move(tmpInstances));


		// BIOME_SHRUBLAND
		// grass
		// Dandelion
		// Cornflower 
		// Allium 
		// Oxeye Daisy 
		// tulips
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SHRUBLAND].SetBaseColor(RGBA_UINT(163, 184, 99, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SHRUBLAND].SetMaxInstanceCountPerChunk(125);
		tmpInstances.clear();
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_SHORT_GRASS));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_DANDELION));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_CORN_FLOWER));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_ALLIUM));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_OXEYE_DAISY));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_TULIP_PINK));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_TULIP_RED));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_TULIP_WHITE));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_TULIP_ORANGE));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SHRUBLAND].SetInstances(std::move(tmpInstances));


		// BIOME_DESERT
		// dead bush
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_DESERT].SetBaseColor(RGBA_UINT(214, 131, 31, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_DESERT].SetMaxInstanceCountPerChunk(5);
		tmpInstances.clear();
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_DEAD_BUSH));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_DESERT].SetInstances(std::move(tmpInstances));


		// BIOME_RAINFOREST
		// grass
		// Fern 
		// Large Fern
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_RAINFOREST].SetBaseColor(RGBA_UINT(93, 130, 21, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_RAINFOREST].SetMaxInstanceCountPerChunk(125);
		tmpInstances.clear();
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_SHORT_GRASS));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_FERN));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_LARGE_FERN));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_RAINFOREST].SetInstances(std::move(tmpInstances));


		// BIOME_SEASONFOREST
		// grass
		// Allium
		// Lily of the Valley 
		// Rose Bush 
		// Various Tulips
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SEASONFOREST].SetBaseColor(
			RGBA_UINT(255, 192, 247, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SEASONFOREST].SetMaxInstanceCountPerChunk(125);
		tmpInstances.clear();
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_SHORT_GRASS));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_ALLIUM));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_LILY_OF_THE_VALLEY));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_ROSE_PLANTS));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_TULIP_PINK));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_TULIP_RED));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_TULIP_WHITE));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_TULIP_ORANGE));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SEASONFOREST].SetInstances(std::move(tmpInstances));


		// BIOME_SAVANA
		// grass
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SAVANA].SetBaseColor(RGBA_UINT(182, 173, 97, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SAVANA].SetMaxInstanceCountPerChunk(125);
		tmpInstances.clear();
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_SHORT_GRASS));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SAVANA].SetInstances(std::move(tmpInstances));


		// BIOME_SNOWY_TAIGA
		// grass
		// fern
		// large fern
		// sweet berry bush
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SNOWY_TAIGA].SetBaseColor(
			RGBA_UINT(200, 255, 239, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SNOWY_TAIGA].SetMaxInstanceCountPerChunk(125);
		tmpInstances.clear();
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_SHORT_GRASS));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_FERN));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_LARGE_FERN));
		tmpInstances.push_back(Instance(INSTANCE_TYPE::INSTANCE_SWEET_BERRY_BUSH));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SNOWY_TAIGA].SetInstances(std::move(tmpInstances));
	}

	inline const BiomeTypeInfo& GetInfo(BIOME_TYPE type) const { return m_biomeTypeInfoSet[type]; }


private:
	std::vector<BiomeTypeInfo> m_biomeTypeInfoSet;
};