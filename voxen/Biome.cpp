#include "Biome.h"

BiomeTypeInfoSet Biome::m_biomeTypeInfoSet;

RGBA_UINT Biome::GetBaseColor(BIOME_TYPE type)
{
	return m_biomeTypeInfoSet.GetInfo(type).GetBaseColor();
}

uint8_t Biome::GetInstanceCountPerChunk(BIOME_TYPE type)
{ 
	return m_biomeTypeInfoSet.GetInfo(type).GetInstanceCountPerChunk();
}

const std::vector<Instance>& Biome::GetInstances(BIOME_TYPE type)
{
	return m_biomeTypeInfoSet.GetInfo(type).GetInstances();
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
		return BIOME_TUNDRA;
	}

	if (humidity < 0.33f) {
		if (temperature < 0.625f) {
			return BIOME_PLAINS;
		}
		else {
			return BIOME_DESERT;
		}
	}

	if (temperature < 0.3125f) {
		return BIOME_SNOWY_TAIGA;
	}
	if (temperature < 0.375f) {
		return BIOME_TAIGA;
	}

	if (temperature < 0.6875f) {
		if (humidity < 0.55f) {
			return BIOME_SHRUBLAND;
		}
		else if (humidity < 0.77f) {
			return BIOME_FOREST;
		}
		else {
			return BIOME_SWAMP;
		}
	}
	else {
		if (humidity < 0.55f) {
			return BIOME_SAVANA;
		}
		else if (humidity < 0.77f) {
			return BIOME_SEASONFOREST;
		}
		else {
			return BIOME_RAINFOREST;
		}
	}

	return BIOME_PLAINS;
}