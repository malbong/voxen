#include "WorldMap.h"
#include "Graphics.h"
#include "SimpleQuadRenderer.h"
#include "Terrain.h"
#include "DXUtils.h"

WorldMap::WorldMap() {}

WorldMap::~WorldMap() {}

bool WorldMap::Initialize(Vector3 cameraPosition)
{
	// biome map
	{
		m_biomeMapData.resize(BIOME_MAP_BUFFER_SIZE * BIOME_MAP_BUFFER_SIZE);
		m_biomeMapOffsetPosition =
			Utils::CalcOffsetPos(cameraPosition, BIOME_MAP_WORLD_SIZE_PER_PIXEL);
		for (int z = 0; z < BIOME_MAP_BUFFER_SIZE; ++z) {
			for (int x = 0; x < BIOME_MAP_BUFFER_SIZE; ++x) {
				m_biomeMapData[x + z * BIOME_MAP_BUFFER_SIZE] = GetBiomeMapColor(x, z);
			}
		}

		if (!DXUtils::UpdateTexture2DBuffer(Graphics::biomeMapBuffer, m_biomeMapData,
				BIOME_MAP_BUFFER_SIZE, BIOME_MAP_BUFFER_SIZE)) {
			return false;
		}
	}

	// climate map
	{
		m_climateMapData.resize(CLIMATE_MAP_BUFFER_SIZE * CLIMATE_MAP_BUFFER_SIZE);
		m_climateMapOffsetPosition =
			Utils::CalcOffsetPos(cameraPosition, CLIMATE_MAP_WORLD_SIZE_PER_PIXEL);
		for (int z = 0; z < CLIMATE_MAP_BUFFER_SIZE; ++z) {
			for (int x = 0; x < CLIMATE_MAP_BUFFER_SIZE; ++x) {
				m_climateMapData[x + z * CLIMATE_MAP_BUFFER_SIZE] = GetClimateNoise(x, z);
			}
		}

		if (!DXUtils::UpdateTexture2DBuffer(Graphics::climateMapBuffer, m_climateMapData,
				CLIMATE_MAP_BUFFER_SIZE, CLIMATE_MAP_BUFFER_SIZE)) {
			return false;
		}
	}
	return true;
}

void WorldMap::Update(Vector3 cameraPosition)
{
	// biome map
	{
		Vector3 newOffsetPosition =
			Utils::CalcOffsetPos(cameraPosition, BIOME_MAP_WORLD_SIZE_PER_PIXEL);
		Vector3 offsetDiff = m_biomeMapOffsetPosition - newOffsetPosition;
		if (offsetDiff.Length() > 0) {
			m_biomeMapOffsetPosition = newOffsetPosition;

			int dx = Utils::Signf(offsetDiff.x);
			int dz = -Utils::Signf(offsetDiff.z);

			ShiftBiomeMapData(dx, dz);

			UpdateBiomeMapData(dx, dz);

			DXUtils::UpdateTexture2DBuffer(Graphics::biomeMapBuffer, m_biomeMapData,
				BIOME_MAP_BUFFER_SIZE, BIOME_MAP_BUFFER_SIZE);
		}
	}

	// climate map
	{
		Vector3 newOffsetPosition =
			Utils::CalcOffsetPos(cameraPosition, CLIMATE_MAP_WORLD_SIZE_PER_PIXEL);
		Vector3 offsetDiff = m_climateMapOffsetPosition - newOffsetPosition;
		if (offsetDiff.Length() > 0) {
			m_climateMapOffsetPosition = newOffsetPosition;

			int dx = Utils::Signf(offsetDiff.x);
			int dz = -Utils::Signf(offsetDiff.z);

			ShiftClimateMapData(dx, dz);

			UpdateClimateMapData(dx, dz);

			DXUtils::UpdateTexture2DBuffer(Graphics::climateMapBuffer, m_climateMapData,
				CLIMATE_MAP_BUFFER_SIZE, CLIMATE_MAP_BUFFER_SIZE);
		}
	}
}

void WorldMap::RenderBiomeMap()
{
	Graphics::context->RSSetViewports(1, &Graphics::worldMapViewport);

	Graphics::context->OMSetRenderTargets(1, Graphics::backBufferRTV.GetAddressOf(), nullptr);

	std::vector<ID3D11ShaderResourceView*> ppSRVs;
	ppSRVs.push_back(Graphics::biomeMapSRV.Get());
	ppSRVs.push_back(Graphics::worldPointSRV.Get());
	Graphics::context->PSSetShaderResources(0, (UINT)ppSRVs.size(), ppSRVs.data());

	Graphics::SetPipelineStates(Graphics::biomeMapPSO);
	SimpleQuadRenderer::GetInstance()->Render();

	Graphics::context->RSSetViewports(1, &Graphics::basicViewport);
}

void WorldMap::ShiftBiomeMapData(int dx, int dz)
{
	int startX = (dx <= 0) ? 0 : BIOME_MAP_BUFFER_SIZE - 1;
	int endX = (dx <= 0) ? BIOME_MAP_BUFFER_SIZE : -1;
	int stepX = (dx <= 0) ? 1 : -1;

	int startZ = (dz <= 0) ? 0 : BIOME_MAP_BUFFER_SIZE - 1;
	int endZ = (dz <= 0) ? BIOME_MAP_BUFFER_SIZE : -1;
	int stepZ = (dz <= 0) ? 1 : -1;

	for (int z = startZ; z != endZ; z += stepZ) {
		for (int x = startX; x != endX; x += stepX) {
			int nx = x + dx;
			int nz = z + dz;

			if (nx < 0 || nx >= BIOME_MAP_BUFFER_SIZE || nz < 0 || nz >= BIOME_MAP_BUFFER_SIZE)
				continue;

			m_biomeMapData[nx + nz * BIOME_MAP_BUFFER_SIZE] =
				m_biomeMapData[x + z * BIOME_MAP_BUFFER_SIZE];
		}
	}
}

void WorldMap::UpdateBiomeMapData(int dx, int dz)
{
	if (dx != 0) {
		int x = (dx < 0) ? BIOME_MAP_BUFFER_SIZE - 1 : 0;
		for (int z = 0; z < BIOME_MAP_BUFFER_SIZE; ++z) {
			m_biomeMapData[x + z * BIOME_MAP_BUFFER_SIZE] = GetBiomeMapColor(x, z);
		}
	}

	if (dz != 0) {
		int z = (dz < 0) ? BIOME_MAP_BUFFER_SIZE - 1 : 0;
		for (int x = 0; x < BIOME_MAP_BUFFER_SIZE; ++x) {
			m_biomeMapData[x + z * BIOME_MAP_BUFFER_SIZE] = GetBiomeMapColor(x, z);
		}
	}
}

RGBA_UINT WorldMap::GetBiomeMapColor(int x, int z)
{
	int worldX = (int)m_biomeMapOffsetPosition.x - (BIOME_MAP_WORLD_SIZE / 2) +
				 BIOME_MAP_WORLD_SIZE_PER_PIXEL * x;
	int worldZ = (int)m_biomeMapOffsetPosition.z + (BIOME_MAP_WORLD_SIZE / 2) -
				 BIOME_MAP_WORLD_SIZE_PER_PIXEL * z;

	float continentalness = Terrain::GetContinentalness(worldX, worldZ);
	float erosion = Terrain::GetErosion(worldX, worldZ);
	float peaksValley = Terrain::GetPeaksValley(worldX, worldZ);

	float temperature = Terrain::GetTemperature(worldX, worldZ);
	float humidity = Terrain::GetHumidity(worldX, worldZ);

	float elevation = Terrain::GetElevation(continentalness, erosion, peaksValley);

	if (elevation < 64.0f) {
		return RGBA_UINT(91, 89, 255, 255);
	}

	BIOME_TYPE biomeType = Terrain::GetBiomeType(elevation, temperature, humidity, peaksValley, erosion);

	return GetColorByBiome(biomeType);
}

RGBA_UINT WorldMap::GetColorByBiome(BIOME_TYPE biomeType)
{
	switch (biomeType) {

	case BIOME_OCEAN:
		return RGBA_UINT(0, 0, 255, 255);

	case BIOME_BEACH:
		return RGBA_UINT(255, 223, 128, 255);

	case BIOME_TUNDRA:
		return RGBA_UINT(235, 235, 235, 255);

	case BIOME_TAIGA:
		return RGBA_UINT(59, 94, 84, 255);

	case BIOME_PLAINS:
		return RGBA_UINT(128, 160, 91, 255);

	case BIOME_SWAMP:
		return RGBA_UINT(20, 249, 183, 255);

	case BIOME_FOREST:
		return RGBA_UINT(59, 123, 78, 255);

	case BIOME_SHRUBLAND:
		return RGBA_UINT(163, 184, 99, 255);

	case BIOME_DESERT:
		return RGBA_UINT(214, 131, 31, 255);

	case BIOME_RAINFOREST:
		return RGBA_UINT(93, 130, 21, 255);

	case BIOME_SEASONFOREST:
		return RGBA_UINT(255, 192, 247, 255);

	case BIOME_SAVANA:
		return RGBA_UINT(182, 173, 97, 255);

	case BIOME_SNOWY_TAIGA:
		return RGBA_UINT(200, 255, 239, 255);

	default:
		return RGBA_UINT(255, 0, 0, 255);
	}
}

void WorldMap::ShiftClimateMapData(int dx, int dz)
{
	int startX = (dx <= 0) ? 0 : CLIMATE_MAP_BUFFER_SIZE - 1;
	int endX = (dx <= 0) ? CLIMATE_MAP_BUFFER_SIZE : -1;
	int stepX = (dx <= 0) ? 1 : -1;

	int startZ = (dz <= 0) ? 0 : CLIMATE_MAP_BUFFER_SIZE - 1;
	int endZ = (dz <= 0) ? CLIMATE_MAP_BUFFER_SIZE : -1;
	int stepZ = (dz <= 0) ? 1 : -1;

	for (int z = startZ; z != endZ; z += stepZ) {
		for (int x = startX; x != endX; x += stepX) {
			int nx = x + dx;
			int nz = z + dz;

			if (nx < 0 || nx >= CLIMATE_MAP_BUFFER_SIZE || nz < 0 || nz >= CLIMATE_MAP_BUFFER_SIZE)
				continue;

			m_climateMapData[nx + nz * CLIMATE_MAP_BUFFER_SIZE] =
				m_climateMapData[x + z * CLIMATE_MAP_BUFFER_SIZE];
		}
	}
}

void WorldMap::UpdateClimateMapData(int dx, int dz)
{
	if (dx != 0) {
		int x = (dx < 0) ? CLIMATE_MAP_BUFFER_SIZE - 1 : 0;
		for (int z = 0; z < CLIMATE_MAP_BUFFER_SIZE; ++z) {
			m_climateMapData[x + z * CLIMATE_MAP_BUFFER_SIZE] = GetClimateNoise(x, z);
		}
	}

	if (dz != 0) {
		int z = (dz < 0) ? CLIMATE_MAP_BUFFER_SIZE - 1 : 0;
		for (int x = 0; x < CLIMATE_MAP_BUFFER_SIZE; ++x) {
			m_climateMapData[x + z * CLIMATE_MAP_BUFFER_SIZE] = GetClimateNoise(x, z);
		}
	}
}

CLIMATE WorldMap::GetClimateNoise(int x, int z)
{
	int worldX = (int)m_climateMapOffsetPosition.x - (CLIMATE_MAP_WORLD_SIZE / 2) +
				 CLIMATE_MAP_WORLD_SIZE_PER_PIXEL * x;
	int worldZ = (int)m_climateMapOffsetPosition.z + (CLIMATE_MAP_WORLD_SIZE / 2) -
				 CLIMATE_MAP_WORLD_SIZE_PER_PIXEL * z;

	float temperature = Terrain::GetTemperature(worldX, worldZ);
	float humidity = Terrain::GetHumidity(worldX, worldZ);

	return CLIMATE(temperature, humidity);
}