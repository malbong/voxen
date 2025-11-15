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