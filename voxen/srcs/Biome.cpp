#include "Biome.h"
#include "Terrain.h"

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

const BiomeWeightParams& Biome::GetWeightParams(BIOME_TYPE type)
{
	return m_biomeTypeInfoSet.GetInfo(type).GetWeightParams();
}

BIOME_TYPE Biome::GetBiomeType(float c, float e, float t, float h)
{
	BIOME_TYPE retBiome = BIOME_PLAINS;
	float minDiff = FLT_MAX;

	float coEffi[4] = { 10.0f, 1.0f, 3.0f, 3.0f };
	for (int i = 0; i < (int)BIOME_TYPE::BIOME_COUNT; ++i) {
		const BiomeWeightParams& wParams = GetWeightParams((BIOME_TYPE)i);

		float cDiff = coEffi[0] * std::powf(std::fabs(c - wParams.continentalness), 2.0f);
		float eDiff = coEffi[1] * std::powf(std::fabs(e - wParams.erosion), 2.0f);
		float tDiff = coEffi[2] * std::powf(std::fabs(t - wParams.temperature), 2.0f);
		float hDiff = coEffi[3] * std::powf(std::fabs(h - wParams.humidity), 2.0f);
		float diff = std::sqrtf(cDiff + eDiff + tDiff + hDiff);

		if (diff < minDiff) {
			retBiome = (BIOME_TYPE)i;
			minDiff = diff;
		}
	}

	return retBiome;
}

float Biome::GetBiomeTerrainHeight(float c, float e, float pv, float t, float h)
{
	// biomeBaseHeight + (scale * Elevation(c, e, pv));

	float sumBiomeBaseHeight = 0.0f;
	float sumElevationScale = 0.0f;
	float sumWeight = 0.0f;

	float coEffi[4] = { 10.0f, 1.0f, 5.0f, 5.0f };
	for (int i = 0; i < (int)BIOME_TYPE::BIOME_COUNT; ++i) {
		const BiomeWeightParams& wParams = GetWeightParams((BIOME_TYPE)i);

		float cDiff = coEffi[0] * std::powf(std::fabs(c - wParams.continentalness), 2.0f);
		float eDiff = coEffi[1] * std::powf(std::fabs(e - wParams.erosion), 2.0f);
		float tDiff = coEffi[2] * std::powf(std::fabs(t - wParams.temperature), 2.0f);
		float hDiff = coEffi[3] * std::powf(std::fabs(h - wParams.humidity), 2.0f);
		float diff = max(1e-5f, std::sqrtf(cDiff + eDiff + tDiff + hDiff));

		float weight = 1.0f / (diff * diff);
		float weight2 = std::powf(weight, 5.0f);

		sumBiomeBaseHeight += weight2 * wParams.baseHeight;
		sumElevationScale += weight2 * wParams.elevationScale;
		sumWeight += weight2;
	}

	float biomeBaseHeight = sumBiomeBaseHeight / sumWeight;
	float elevationScale = sumElevationScale / sumWeight;
	float elevation = Terrain::GetElevation(c, e, pv);

	float height = biomeBaseHeight + (elevationScale * elevation);

	return std::clamp(height, 1.0f, 255.0f);
}