#pragma once

#include <directxtk/SimpleMath.h>
#include <vector>

#include "ChunkManager.h"

using namespace DirectX::SimpleMath;

class WorldMap {

public:

	static const UINT BIOME_MAP_PIXEL_SIZE = sizeof(RGBA_UINT);
	static const UINT BIOME_MAP_UI_SIZE = 720;
	static const UINT BIOME_MAP_BUFFER_SIZE = 512;
	static const UINT BIOME_MAP_WORLD_SIZE_PER_PIXEL =
		(ChunkManager::CHUNK_COUNT * Chunk::CHUNK_SIZE * 2) / BIOME_MAP_BUFFER_SIZE;
	static const UINT BIOME_MAP_WORLD_SIZE = BIOME_MAP_BUFFER_SIZE * BIOME_MAP_WORLD_SIZE_PER_PIXEL;

	static const UINT CLIMATE_MAP_BUFFER_SIZE = ChunkManager::CHUNK_COUNT * Chunk::CHUNK_SIZE;
	static const UINT CLIMATE_MAP_WORLD_SIZE_PER_PIXEL = 1;
	static const UINT CLIMATE_MAP_WORLD_SIZE = CLIMATE_MAP_BUFFER_SIZE * CLIMATE_MAP_WORLD_SIZE_PER_PIXEL;

	WorldMap();
	~WorldMap();

	bool Initialize(Vector3 cameraPosition);
	void Update(Vector3 cameraPosition);

	void RenderBiomeMap();


private:
	std::vector<RGBA_UINT> m_biomeMapData;
	Vector3 m_biomeMapOffsetPosition;

	std::vector<CLIMATE> m_climateMapData;
	Vector3 m_climateMapOffsetPosition;

	void ShiftBiomeMapData(int dx, int dz);
	void UpdateBiomeMapData(int dx, int dz);
	RGBA_UINT GetBiomeMapColor(int x, int z);

	void ShiftClimateMapData(int dx, int dz);
	void UpdateClimateMapData(int dx, int dz);
	CLIMATE GetClimateNoise(int x, int z);
};
