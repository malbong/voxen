#include "Biome.h"

BiomeTypeInfoSet Biome::m_biomeTypeInfoSet;

RGBA_UINT Biome::GetBaseColor(BIOME_TYPE type)
{
	return m_biomeTypeInfoSet.GetInfo(type).GetBaseColor();
}

uint32_t Biome::GetMaxTreeCountPerChunk(BIOME_TYPE type)
{
	return m_biomeTypeInfoSet.GetInfo(type).GetMaxTreeCountPerChunk();
}

uint32_t Biome::GetMaxInstanceCountPerChunk(BIOME_TYPE type)
{ 
	return m_biomeTypeInfoSet.GetInfo(type).GetMaxInstanceCountPerChunk();
}

const std::vector<INSTANCE_TYPE>& Biome::GetInstances(BIOME_TYPE type)
{
	return m_biomeTypeInfoSet.GetInfo(type).GetInstances();
}

const std::vector<TREE_TYPE>& Biome::GetTrees(BIOME_TYPE type)
{
	return m_biomeTypeInfoSet.GetInfo(type).GetTrees();
}

BIOME_TYPE Biome::GetBiomeType(
	float elevation, float temperature, float humidity, float peaksValley, float erosion)
{
	// Biome Block
	float pvRange = 32.0f * peaksValley * powf((1.0f - erosion), 1.25f);
	float newElevation = elevation - pvRange;

	if (newElevation < 64.0f) {
		return BIOME_OCEAN;
	}
	else if (newElevation < 68.0f) {
		return BIOME_BEACH;
	}

	if (temperature < 0.25f) {
		return BIOME_TUNDRA; // t < 0.25 && h < 1
	}

	if (humidity < 0.33f) {
		if (temperature < 0.625f) {
			return BIOME_PLAINS; // 0.25 < t < 0.625 && h < 0.33
		}
		else {
			return BIOME_DESERT; // 0.625 < t && h < 0.33
		}
	}

	if (temperature < 0.3125f) {
		return BIOME_SNOWY_TAIGA; // 0.25 < t < 0.3125 && 0.33 < h < 1
	}
	if (temperature < 0.375f) { // 0.3125 < t < 0.375 && 0.33 < h < 1
		return BIOME_TAIGA;
	}

	if (temperature < 0.6875f) {
		if (humidity < 0.55f) {
			return BIOME_SHRUBLAND; // 0.375 < t < 0.6875 && 0.33 < h < 0.55
		}
		else if (humidity < 0.77f) { 
			return BIOME_FOREST; // 0.375 < t < 0.6875 && 0.55 < h < 0.77
		}
		else {
			return BIOME_SWAMP; // 0.375 < t < 0.6875 && 0.77 < h
		}
	}
	else {
		if (humidity < 0.55f) { // 0.6875 < t && 0.33 < h < 0.55
			return BIOME_SAVANNA;
		}
		else if (humidity < 0.77f) { // 0.6875 < t && 0.55 < h < 0.77
			return BIOME_SEASONFOREST;
		}
		else {
			return BIOME_RAINFOREST; // // 0.6875 < t && 0.77 < h
		}
	}

	return BIOME_PLAINS;
}