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
	static uint8_t GetInstanceCountPerChunk(BIOME_TYPE type);
	static const std::vector<Instance>& GetInstances(BIOME_TYPE type);
	static BIOME_TYPE GetBiomeType(
		float elevation, float temperature, float humidity, float peaksValley, float erosion);

private:
	static BiomeTypeInfoSet m_biomeTypeInfoSet;
};


class BiomeTypeInfo {

public:
	BiomeTypeInfo() : m_baseColor(RGBA_UINT(0, 0, 0, 0)), m_instanceCountPerChunk(0), m_instances()
	{
	}
	~BiomeTypeInfo() {}

	inline RGBA_UINT GetBaseColor() const { return m_baseColor; }
	inline uint8_t GetInstanceCountPerChunk() const { return m_instanceCountPerChunk; }
	inline const std::vector<Instance>& GetInstances() const { return m_instances; }

	inline void SetBaseColor(RGBA_UINT baseColor) { m_baseColor = baseColor; }
	inline void SetInstanceCountPerChunk(uint8_t instanceCountPerChunk)
	{
		m_instanceCountPerChunk = instanceCountPerChunk;
	}
	inline void SetInstances(std::vector<Instance>&& instances)
	{
		m_instances = std::move(instances);
	}

private:
	RGBA_UINT m_baseColor;
	uint8_t m_instanceCountPerChunk;
	std::vector<Instance> m_instances;
};


class BiomeTypeInfoSet {

public:
	BiomeTypeInfoSet() : m_biomeTypeInfoSet(Biome::BIOME_TYPE_COUNT)
	{
		// BIOME_OCEAN
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_OCEAN].SetBaseColor(RGBA_UINT(0, 0, 255, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_OCEAN].SetInstanceCountPerChunk(5);

		// BIOME_BEACH
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_BEACH].SetBaseColor(RGBA_UINT(255, 223, 128, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_BEACH].SetInstanceCountPerChunk(5);

		// BIOME_TUNDRA
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_TUNDRA].SetBaseColor(RGBA_UINT(235, 235, 235, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_TUNDRA].SetInstanceCountPerChunk(5);

		// BIOME_TAIGA
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_TAIGA].SetBaseColor(RGBA_UINT(59, 94, 84, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_TAIGA].SetInstanceCountPerChunk(5);

		// BIOME_PLAINS
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_PLAINS].SetBaseColor(RGBA_UINT(128, 169, 91, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_PLAINS].SetInstanceCountPerChunk(5);

		// BIOME_SWAMP
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SWAMP].SetBaseColor(RGBA_UINT(20, 249, 183, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SWAMP].SetInstanceCountPerChunk(5);

		// BIOME_FOREST
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_FOREST].SetBaseColor(RGBA_UINT(59, 123, 78, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_FOREST].SetInstanceCountPerChunk(5);

		// BIOME_SHRUBLAND
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SHRUBLAND].SetBaseColor(RGBA_UINT(163, 184, 99, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SHRUBLAND].SetInstanceCountPerChunk(5);

		// BIOME_DESERT
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_DESERT].SetBaseColor(RGBA_UINT(214, 131, 31, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_DESERT].SetInstanceCountPerChunk(5);

		// BIOME_RAINFOREST
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_RAINFOREST].SetBaseColor(RGBA_UINT(93, 130, 21, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_RAINFOREST].SetInstanceCountPerChunk(5);

		// BIOME_SEASONFOREST
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SEASONFOREST].SetBaseColor(
			RGBA_UINT(255, 192, 247, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SEASONFOREST].SetInstanceCountPerChunk(5);

		// BIOME_SAVANA
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SAVANA].SetBaseColor(RGBA_UINT(182, 173, 97, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SAVANA].SetInstanceCountPerChunk(5);

		// BIOME_SNOWY_TAIGA
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SNOWY_TAIGA].SetBaseColor(
			RGBA_UINT(200, 255, 239, 255));
		m_biomeTypeInfoSet[BIOME_TYPE::BIOME_SNOWY_TAIGA].SetInstanceCountPerChunk(5);
	}

	inline const BiomeTypeInfo& GetInfo(BIOME_TYPE type) const { return m_biomeTypeInfoSet[type]; }


private:
	std::vector<BiomeTypeInfo> m_biomeTypeInfoSet;
};