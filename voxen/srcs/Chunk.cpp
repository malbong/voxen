#include "Chunk.h"
#include "DXUtils.h"
#include "MeshGenerator.h"
#include "ChunkManager.h"
#include "Biome.h"
#include "Tree.h"

#include <future>
#include <algorithm>
#include <unordered_map>

Chunk::Chunk(UINT id)
	: m_id(id), m_isLoaded(false), m_isPatching(false), m_offsetPosition(0.0f, 0.0f, 0.0f),
	  m_position(0.0f, 0.0f, 0.0f), m_isUpdateRequired(true)
{
}

Chunk::~Chunk() { Clear(); }

ChunkLoadMemory* Chunk::Initialize(ChunkLoadMemory* memory)
{
	////////////////////////////////////
	// check start time
	static long long sum = 0;
	static long long count = 0;
	auto start_time = std::chrono::steady_clock::now();
	////////////////////////////////////

	// initialize noises for terrain
	InitTerrainNoises(memory);

	// initialize biome count
	InitBiomeMapAndCount(memory);

	// initialize block type of basic block
	InitBasicBlockType(memory);

	// initialize tree place and make modified chunk
	InitTreePlace(memory);

	// initalize instance place by seed random
	InitInstancePlace(memory);

	// initialize world(opaque & water & semiAlpha) vertice data by greedy meshing
	InitWorldVerticesData(memory);

	// initialize constant data
	m_position = Vector3(m_offsetPosition.x, -2.0f * CHUNK_SIZE, m_offsetPosition.z);
	m_constantData.world = Matrix::CreateTranslation(m_position);

	////////////////////////////////////
	// check end time
	auto end_time = std::chrono::steady_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
	sum += duration.count();
	count++;

	// std::cout << "duration: " << duration.count() << " micro s"
	//		  << " | "
	//		  << "average: " << (float)sum / (float)count << " micro s" << std::endl;
	////////////////////////////////////

	return memory;
}

ChunkLoadMemory* Chunk::Patch(const PatchDataHashSet& patchDataSet, ChunkLoadMemory* memory)
{
	m_isPatching = true;
	m_onPatchDirtyFlag = false;

	for (const PatchData& patchData : patchDataSet) {
		int x = patchData.localX;
		int y = patchData.localY;
		int z = patchData.localZ;

		// for instance
		if (patchData.instance.GetType() != INSTANCE_TYPE::INSTANCE_NONE &&
			Block::IsTransparency(m_blocks[x + 1][y + 1][z + 1].GetType())) {

			if (IsInstanceAt(x, y, z)) {
				m_instanceMap.erase(PosInt3(x, y, z));
			}

			m_instanceMap.insert(std::pair(PosInt3(x, y, z), Instance(patchData.instance)));
		}

		// for block
		if (patchData.block.GetType() != BLOCK_TYPE::BLOCK_NONE) {
			if (patchData.block.GetType() != m_blocks[x + 1][y + 1][z + 1].GetType()) {
				m_blocks[x + 1][y + 1][z + 1] = Block(patchData.block);
				m_onPatchDirtyFlag = true;
			}

			if (IsInstanceAt(x, y, z)) {
				m_instanceMap.erase(PosInt3(x, y, z));
			}
		}
	}

	if (m_onPatchDirtyFlag) {
		InitWorldVerticesData(memory);
	}

	return memory;
}

void Chunk::Update(float dt)
{
	if (m_isUpdateRequired) {
		m_position.y += 50.0f * dt;

		if (m_position.y > m_offsetPosition.y) {
			m_position.y = m_offsetPosition.y;
			
			m_isUpdateRequired = false;
		}

		m_constantData.world = Matrix::CreateTranslation(m_position);
	}
}

void Chunk::Clear()
{
	m_isLoaded = false;
	m_isPatching = false;
	m_isUpdateRequired = false;
	m_onPatchDirtyFlag = false;

	m_instanceMap.clear();

	ClearCpuVertices();

	UpdateCpuBufferCount();
}

void Chunk::ClearCpuVertices()
{
	m_lowLodVertices.clear();
	m_lowLodIndices.clear();

	m_opaqueVertices.clear();
	m_opaqueIndices.clear();

	m_transparencyVertices.clear();
	m_transparencyIndices.clear();

	m_semiAlphaVertices.clear();
	m_semiAlphaIndices.clear();
}

void Chunk::UpdateCpuBufferCount()
{
	// patching 멀티쓰레드로 인한 Count 값 업데이트
	// 직접 vector에 접근하여 size를 가져와 사용하는 건 멀티쓰레드의 데이터레이스를 피할 수 없음
	m_lowLodVertexCount = (uint32_t)m_lowLodVertices.size();
	m_lowLodIndexCount = (uint32_t)m_lowLodIndices.size();

	m_opaqueVertexCount = (uint32_t)m_opaqueVertices.size();
	m_opaqueIndexCount = (uint32_t)m_opaqueIndices.size();

	m_transparencyVertexCount = (uint32_t)m_transparencyVertices.size();
	m_transparencyIndexCount = (uint32_t)m_transparencyIndices.size();

	m_semiAlphaVertexCount = (uint32_t)m_semiAlphaVertices.size();
	m_semiAlphaIndexCount = (uint32_t)m_semiAlphaIndices.size();
}

void Chunk::InitTerrainNoises(ChunkLoadMemory* memory)
{
	for (int x = 0; x < CHUNK_SIZE_P; ++x) {
		for (int z = 0; z < CHUNK_SIZE_P; ++z) {
			int worldX = (int)m_offsetPosition.x + x - 1;
			int worldZ = (int)m_offsetPosition.z + z - 1;

			memory->continentalinessNoises[x][z] = Terrain::GetContinentalness(worldX, worldZ);
			memory->erosionNoises[x][z] = Terrain::GetErosion(worldX, worldZ);
			memory->peaksValleyNoises[x][z] = Terrain::GetPeaksValley(worldX, worldZ);
			memory->temperatureNoises[x][z] = Terrain::GetTemperature(worldX, worldZ);
			memory->humidityNoises[x][z] = Terrain::GetHumidity(worldX, worldZ);
			memory->distributionNoises[x][z] = Terrain::GetDistribution(worldX, worldZ);
			memory->elevationNoises[x][z] =
				Biome::GetBiomeTerrainHeight(memory->continentalinessNoises[x][z],
					memory->erosionNoises[x][z], memory->peaksValleyNoises[x][z],
					memory->temperatureNoises[x][z], memory->humidityNoises[x][z]);
		}
	}
}

void Chunk::InitBiomeMapAndCount(ChunkLoadMemory* memory)
{
	for (int x = 0; x < CHUNK_SIZE; ++x) {
		for (int z = 0; z < CHUNK_SIZE; ++z) {
			int px = x + 1;
			int pz = z + 1;

			int worldX = (int)m_offsetPosition.x + x;
			int worldZ = (int)m_offsetPosition.z + z;

			BIOME_TYPE biomeType = Biome::GetBiomeType(memory->continentalinessNoises[px][pz],
				memory->erosionNoises[px][pz], memory->temperatureNoises[px][pz],
				memory->humidityNoises[px][pz], worldX, worldZ);

			memory->biomeMap2D[x][z] = biomeType;

			memory->biomeCount[biomeType]++;
		}
	}
}

void Chunk::InitBasicBlockType(ChunkLoadMemory* memory)
{
	for (int x = 0; x < CHUNK_SIZE_P; ++x) {
		for (int y = 0; y < CHUNK_SIZE_P; ++y) {
			for (int z = 0; z < CHUNK_SIZE_P; ++z) {
				int worldX = (int)m_offsetPosition.x + x - 1;
				int worldY = (int)m_offsetPosition.y + y - 1;
				int worldZ = (int)m_offsetPosition.z + z - 1;

				BLOCK_TYPE blockType = Block::GetBlockType(worldX, worldY, worldZ,
					memory->continentalinessNoises[x][z], memory->erosionNoises[x][z],
					memory->peaksValleyNoises[x][z], memory->temperatureNoises[x][z],
					memory->humidityNoises[x][z], memory->distributionNoises[x][z],
					memory->elevationNoises[x][z]);

				m_blocks[x][y][z].SetType(blockType);
			}
		}
	}
}

uint32_t Chunk::GetMaxPlaceCountByBiomeRatio(
	BIOME_TYPE biomeType, int maxCountPerChunk, int biomeCount)
{
	float biomeRatio = biomeCount / (float)CHUNK_SIZE2;

	float placeCountByRatio = biomeRatio * (float)maxCountPerChunk;

	return (uint32_t)placeCountByRatio;
}

void Chunk::InitTreePlace(ChunkLoadMemory* memory)
{
	Terrain::GenerateRandomPlace2D(m_offsetPosition, TREE_PLACE_RANDOM_SOLT_X,
		TREE_PLACE_RANDOM_SOLT_Z, TREE_PLACE_MAX_COUNT_PER_CHUNK, CHUNK_SIZE,
		memory->treeRandomPlace2D);

	uint32_t placedBiomeTreeCount[Biome::BIOME_TYPE_COUNT] = {
		0,
	};

	for (int i = 0; i < TREE_PLACE_MAX_COUNT_PER_CHUNK; ++i) {
		int x = memory->treeRandomPlace2D[i].first;
		int z = memory->treeRandomPlace2D[i].second;

		BIOME_TYPE biomeType = memory->biomeMap2D[x][z];

		float elevationWorldY = std::floor(memory->elevationNoises[x + 1][z + 1]);
		int localY = (int)(elevationWorldY - m_offsetPosition.y);

		if (CanPlaceTreeAt(x, localY, z, placedBiomeTreeCount[biomeType], memory)) {
			TREE_TYPE treeType = Tree::GetTreeTypeForBiome(
				biomeType, memory->distributionNoises[x + 1][z + 1], x, localY, z);

			PlaceTree(x, localY, z, memory, treeType);

			placedBiomeTreeCount[biomeType]++;
		}
	}
}

bool Chunk::CanPlaceTreeAt(
	int x, int y, int z, uint32_t placedBiomeTreeCount, ChunkLoadMemory* memory)
{
	// set ratio max tree count per chunk for biome
	BIOME_TYPE biomeType = memory->biomeMap2D[x][z];
	uint32_t maxTreeCountByRatio = GetMaxPlaceCountByBiomeRatio(
		biomeType, Biome::GetMaxTreeCountPerChunk(biomeType), memory->biomeCount[biomeType]);
	if (placedBiomeTreeCount >= maxTreeCountByRatio) {
		return false;
	}

	// compared world elevation with localY range
	if (!IsInsideChunk(x, y, z)) {
		return false;
	}

	// get tree type
	TREE_TYPE treeType =
		Tree::GetTreeTypeForBiome(biomeType, memory->distributionNoises[x + 1][z + 1], x, y, z);
	if (treeType == TREE_TYPE::TREE_NONE) {
		return false;
	}

	// checked place conditions
	for (int i = -1; i <= 1; ++i) {
		if (!CheckTreePlaceCondition(x + i, y, z)) {
			return false;
		}
		if (!CheckTreePlaceCondition(x, y, z + i)) {
			return false;
		}
	}

	return true;
}

bool Chunk::CheckTreePlaceCondition(int x, int y, int z)
{
	BLOCK_TYPE currentBlockType = m_blocks[x + 1][y + 1][z + 1].GetType();
	BLOCK_TYPE topBlockType = m_blocks[x + 1][y + 2][z + 1].GetType();

	if (!Block::IsOpaque(currentBlockType))
		return false;

	if (topBlockType != BLOCK_TYPE::BLOCK_AIR)
		return false;

	return true;
}

void Chunk::SetTreeBlockType(int tx, int ty, int tz, BLOCK_TYPE treeBlock, ChunkLoadMemory* memory)
{
	// Set chunk tree block
	if (IsInsideChunkWithPadding(tx, ty, tz)) { // -1 <= tx <= 32
		m_blocks[tx + 1][ty + 1][tz + 1].SetType(treeBlock);
	}

	// Making chunk patch data to neighbor chunk
	if (!IsInsideChunk(tx, ty, tz)) {
		PatchData patchData = ChunkManager::GetInstance()->MakePatchData(
			tx, ty, tz, treeBlock, Instance(), CHUNK_SIZE, true);

		Vector3 blockPos = m_offsetPosition + Vector3((float)tx, (float)ty, (float)tz);
		Vector3 blockOwnerOffsetPos = Utils::CalcOffsetPos(blockPos, CHUNK_SIZE);
		PosInt3 blockOwnerOffsetPosInt3 = Utils::VectorToPosInt3(blockOwnerOffsetPos);

		memory->chunkPatchDataMap[blockOwnerOffsetPosInt3].insert(patchData);
	}

	// Propagation patch for greedy mesh
	if (IsInnerEdge(tx, ty, tz) || IsOuterEdge(tx, ty, tz)) {
		Vector3 blockPos = m_offsetPosition + Vector3((float)tx, (float)ty, (float)tz);
		Vector3 blockOwnerOffsetPos = Utils::CalcOffsetPos(blockPos, CHUNK_SIZE);
		PosInt3 blockOwnerOffsetPosInt3 = Utils::VectorToPosInt3(blockOwnerOffsetPos);

		int localX = Utils::WrapToBase(tx, CHUNK_SIZE);
		int localY = Utils::WrapToBase(ty, CHUNK_SIZE);
		int localZ = Utils::WrapToBase(tz, CHUNK_SIZE);

		std::pair<PosInt3, PatchData> outEdgePatchEntry[3];
		int outEdgePatchEntryCount = 0;
		ChunkManager::GetInstance()->GenerateEdgePatchEntry(localX, localY, localZ,
			blockOwnerOffsetPos, treeBlock, outEdgePatchEntry, outEdgePatchEntryCount);

		PosInt3 myOffsetPosInt3 = Utils::VectorToPosInt3(m_offsetPosition);
		for (int i = 0; i < outEdgePatchEntryCount; ++i) {
			PosInt3& patchChunkPosInt3 = outEdgePatchEntry[i].first;
			PatchData& patchData = outEdgePatchEntry[i].second;

			if (patchChunkPosInt3 != myOffsetPosInt3) {
				memory->chunkPatchDataMap[patchChunkPosInt3].insert(patchData);
			}
		}
	}
}

void Chunk::SetTreeVines(
	int tx, int ty, int tz, INSTANCE_TYPE treeVine, uint8_t faceFlag, ChunkLoadMemory* memory)
{
	PosInt3 worldPosInt3 =
		Utils::VectorToPosInt3(m_offsetPosition + Vector3((float)tx, (float)ty, (float)tz));

	TEXTURE_INDEX texIndex = Instance::GetTextureIndex(treeVine);

	Instance instance = Instance(treeVine, texIndex, 0.0f, Vector2(0.0f), faceFlag);

	if (IsInsideChunk(tx, ty, tz)) {
		m_instanceMap.insert(std::pair(PosInt3(tx, ty, tz), instance));
	}
	else {
		PatchData patchData = ChunkManager::GetInstance()->MakePatchData(
			tx, ty, tz, Block(), instance, CHUNK_SIZE, true);

		Vector3 instancePos = m_offsetPosition + Vector3((float)tx, (float)ty, (float)tz);
		Vector3 instanceOwnerOffsetPos = Utils::CalcOffsetPos(instancePos, CHUNK_SIZE);
		PosInt3 instanceOwnerOffsetPosInt3 = Utils::VectorToPosInt3(instanceOwnerOffsetPos);

		memory->chunkPatchDataMap[instanceOwnerOffsetPosInt3].insert(patchData);
	}
}

void Chunk::PlaceTree(int x, int y, int z, ChunkLoadMemory* memory, TREE_TYPE treeType)
{
	PosInt3 worldPosInt3 =
		Utils::VectorToPosInt3(m_offsetPosition + Vector3((float)x, (float)y, (float)z));
	TreeShape treeShape = { TREE_BLOCK_INDEX::EMPTY };
	Tree::GenerateTreeShape(treeType, worldPosInt3, treeShape);

	for (int dy = 0; dy < Tree::TREE_SIZE; ++dy) {
		for (int dz = 0; dz < Tree::TREE_SIZE; ++dz) {
			for (int dx = 0; dx < Tree::TREE_SIZE; ++dx) {
				if (treeShape[dy][dz][dx] == TREE_BLOCK_INDEX::EMPTY) {
					continue;
				}

				int ty = y + dy + 1;
				int tz = z - dz + (Tree::TREE_SIZE / 2);
				int tx = x + dx - (Tree::TREE_SIZE / 2);

				if (treeShape[dy][dz][dx] == TREE_BLOCK_INDEX::TRUNK ||
					treeShape[dy][dz][dx] == TREE_BLOCK_INDEX::LEAF) {

					BLOCK_TYPE treeBlock = (treeShape[dy][dz][dx] == TREE_BLOCK_INDEX::TRUNK)
											   ? Tree::GetTrunkBlockType(treeType)
											   : Tree::GetLeafBlockType(treeType);

					SetTreeBlockType(tx, ty, tz, treeBlock, memory);
				}

				if (treeShape[dy][dz][dx] >= TREE_BLOCK_INDEX::VINE) {
					INSTANCE_TYPE vineType = INSTANCE_TYPE::INSTANCE_VINE;

					uint8_t faceFlag =
						(treeShape[dy][dz][dx] & (~TREE_BLOCK_INDEX::VINE)); // off vine flag

					SetTreeVines(tx, ty, tz, vineType, faceFlag, memory);
				}
			}
		}
	}
}

bool Chunk::IsInsideChunk(int x, int y, int z)
{
	return (0 <= x && x < CHUNK_SIZE && 0 <= y && y < CHUNK_SIZE && 0 <= z && z < CHUNK_SIZE);
}

bool Chunk::IsInsideChunkWithPadding(int x, int y, int z)
{
	return (-1 <= x && x < CHUNK_SIZE + 1 && -1 <= y && y < CHUNK_SIZE + 1 && -1 <= z &&
			z < CHUNK_SIZE + 1);
}

bool Chunk::IsInnerEdge(int x, int y, int z)
{
	return (x == 0 || x == CHUNK_SIZE - 1 || y == 0 || y == CHUNK_SIZE - 1 || z == 0 ||
			z == CHUNK_SIZE - 1);
}

bool Chunk::IsOuterEdge(int x, int y, int z)
{
	return (x == -1 || x == CHUNK_SIZE || y == -1 || y == CHUNK_SIZE || z == -1 || z == CHUNK_SIZE);
}

void Chunk::InitInstancePlace(ChunkLoadMemory* memory)
{
	Terrain::GenerateRandomPlace2D(m_offsetPosition, INSTANCE_PLACE_RANDOM_SOLT_X,
		INSTANCE_PLACE_RANDOM_SOLT_Z, INSTANCE_PLACE_MAX_COUNT_PER_CHUNK, CHUNK_SIZE,
		memory->instanceRandomPlace2D);

	uint32_t placedBiomeInstanceCount[Biome::BIOME_TYPE_COUNT] = {
		0,
	};

	for (int i = 0; i < INSTANCE_PLACE_MAX_COUNT_PER_CHUNK; ++i) {
		int x = memory->instanceRandomPlace2D[i].first;
		int z = memory->instanceRandomPlace2D[i].second;

		// set water plane instance
		if (CanPlaceWaterPlaneInstanceAt(x, z, memory)) {
			INSTANCE_TYPE instanceType = GetWaterPlaneInstanceType(x, z, memory);

			SetWaterPlaneInstance(x, z, instanceType, memory);
		}

		// set biome instance
		BIOME_TYPE biomeType = memory->biomeMap2D[x][z];

		float elevationWorldY = std::ceil(memory->elevationNoises[x + 1][z + 1]);
		int y = (int)(elevationWorldY - m_offsetPosition.y);

		if (CanPlaceBiomeInstanceAt(x, y, z, placedBiomeInstanceCount[biomeType], memory)) {
			INSTANCE_TYPE instanceType = GetBiomeInstanceType(x, y, z, memory);

			SetBiomeInstance(x, y, z, instanceType, memory);

			placedBiomeInstanceCount[biomeType]++;
		}
	}
}

bool Chunk::IsInstanceAt(int x, int y, int z)
{
	return m_instanceMap.find(PosInt3(x, y, z)) != m_instanceMap.end();
}

INSTANCE_TYPE Chunk::GetWaterPlaneInstanceType(int x, int z, ChunkLoadMemory* memory)
{
	PosInt3 worldPosInt3 =
		Utils::VectorToPosInt3(m_offsetPosition + Vector3((float)x, 0.0f, (float)z));

	float distribution = memory->distributionNoises[x + 1][z + 1];
	float temperature = memory->temperatureNoises[x + 1][z + 1];
	float humidity = memory->humidityNoises[x + 1][z + 1];

	INSTANCE_TYPE instanceType =
		Instance::GetInstanceTypeForWaterPlane(temperature, humidity, distribution, worldPosInt3);

	return instanceType;
}

bool Chunk::CanPlaceWaterPlaneInstanceAt(int x, int z, ChunkLoadMemory* memory)
{
	const float WATER_HEIGHT = 64.0f;

	if (m_offsetPosition.y != WATER_HEIGHT)
		return false;

	float elevationWorldY = std::ceil(memory->elevationNoises[x + 1][z + 1]);
	if (elevationWorldY >= WATER_HEIGHT)
		return false;

	INSTANCE_TYPE instanceType = GetWaterPlaneInstanceType(x, z, memory);
	if (instanceType == INSTANCE_TYPE::INSTANCE_NONE)
		return false;

	return true;
}

void Chunk::SetWaterPlaneInstance(int x, int z, INSTANCE_TYPE instanceType, ChunkLoadMemory* memory)
{
	TEXTURE_INDEX texIndex = Instance::GetTextureIndex(instanceType);

	PosInt3 worldPosInt3 =
		Utils::VectorToPosInt3(m_offsetPosition + Vector3((float)x, 0.0f, (float)z));
	float rangeRotation = (float)Utils::RandomRangeByPos(worldPosInt3, 0, 360);

	float rangeOffsetNoiseX = (float)(Utils::RandomRangeByPos(worldPosInt3, 0, 50) - 25) / 100.0f;
	float rangeOffsetNoiseZ = (float)(Utils::RandomRangeByPos(worldPosInt3, 0, 50) - 25) / 100.0f;
	Vector2 rangeOffsetNoiseXZ = Vector2(rangeOffsetNoiseX, rangeOffsetNoiseZ);

	Instance instance = Instance(instanceType, texIndex, rangeRotation, rangeOffsetNoiseXZ);

	m_instanceMap.insert(std::pair(PosInt3(x, 0, z), instance));
}

INSTANCE_TYPE Chunk::GetBiomeInstanceType(int x, int y, int z, ChunkLoadMemory* memory)
{
	BIOME_TYPE biomeType = memory->biomeMap2D[x][z];

	PosInt3 worldPosInt3 =
		Utils::VectorToPosInt3(m_offsetPosition + Vector3((float)x, (float)y, (float)z));

	INSTANCE_TYPE instanceType = Instance::GetInstanceTypeForBiome(
		biomeType, memory->distributionNoises[x + 1][z + 1], worldPosInt3);

	return instanceType;
}

bool Chunk::CanPlaceBiomeInstanceAt(
	int x, int y, int z, uint32_t placedBiomeInstanceCount, ChunkLoadMemory* memory)
{
	BIOME_TYPE biomeType = memory->biomeMap2D[x][z];
	uint32_t maxInstanceCountByRatio = GetMaxPlaceCountByBiomeRatio(
		biomeType, Biome::GetMaxInstanceCountPerChunk(biomeType), memory->biomeCount[biomeType]);
	if (placedBiomeInstanceCount >= maxInstanceCountByRatio) {
		return false;
	}

	if (!IsInsideChunk(x, y, z)) {
		return false;
	}

	if (IsInstanceAt(x, y, z)) {
		return false;
	}

	INSTANCE_TYPE instanceType = GetBiomeInstanceType(x, y, z, memory);
	if (instanceType == INSTANCE_TYPE::INSTANCE_NONE) {
		return false;
	}

	if (!CheckInstancePlaceCondition(instanceType, x, y, z)) {
		return false;
	}

	return true;
}

bool Chunk::CheckInstancePlaceCondition(INSTANCE_TYPE type, int x, int y, int z)
{
	BLOCK_TYPE currentBlockType = m_blocks[x + 1][y + 1][z + 1].GetType();
	BLOCK_TYPE bottomBlockType = m_blocks[x + 1][y][z + 1].GetType();

	return Instance::CanPlace(type, currentBlockType, bottomBlockType);
}

void Chunk::SetBiomeInstance(
	int x, int y, int z, INSTANCE_TYPE instanceType, ChunkLoadMemory* memory)
{
	PosInt3 worldPosInt3 =
		Utils::VectorToPosInt3(m_offsetPosition + Vector3((float)x, (float)y, (float)z));
	float rangeRotation = (float)Utils::RandomRangeByPos(worldPosInt3, 0, 360);

	float rangeOffsetNoiseX = (float)(Utils::RandomRangeByPos(worldPosInt3, 0, 50) - 25) / 100.0f;
	float rangeOffsetNoiseZ = (float)(Utils::RandomRangeByPos(worldPosInt3, 0, 50) - 25) / 100.0f;
	Vector2 rangeOffsetNoiseXZ = Vector2(rangeOffsetNoiseX, rangeOffsetNoiseZ);

	int instanceMaxHeight = Instance::GetMaxHeight(instanceType);
	float temperature = memory->temperatureNoises[x + 1][z + 1];
	float humidity = memory->humidityNoises[x + 1][z + 1];
	float thPercent = (temperature + humidity) * 0.5f;
	int thMaxHeight = (int)std::ceil(thPercent * (float)instanceMaxHeight);
	int rangeHeight = Utils::RandomRangeByPos(worldPosInt3, 1, thMaxHeight);

	for (int h = 0; h < rangeHeight; ++h) {
		TEXTURE_INDEX texIndex = Instance::GetTextureIndexByHeight(instanceType, h, rangeHeight);

		Instance instance = Instance(instanceType, texIndex, rangeRotation, rangeOffsetNoiseXZ);

		if (IsInsideChunk(x, y + h, z)) {
			m_instanceMap.insert(std::pair(PosInt3(x, y + h, z), instance));
		}
		else {
			PatchData patchData = ChunkManager::GetInstance()->MakePatchData(
				x, y + h, z, Block(), instance, CHUNK_SIZE, true);

			Vector3 instancePos = m_offsetPosition + Vector3((float)x, (float)(y + h), (float)z);
			Vector3 instanceOwnerOffsetPos = Utils::CalcOffsetPos(instancePos, CHUNK_SIZE);
			PosInt3 instanceOwnerOffsetPosInt3 = Utils::VectorToPosInt3(instanceOwnerOffsetPos);

			memory->chunkPatchDataMap[instanceOwnerOffsetPosInt3].insert(patchData);
		}
	}
}

void Chunk::InitWorldVerticesData(ChunkLoadMemory* memory)
{
	// 1. make axis column bit data
	std::unordered_map<BLOCK_TYPE, bool> llTypeMap;
	std::unordered_map<BLOCK_TYPE, bool> opTypeMap;
	std::unordered_map<BLOCK_TYPE, bool> tpTypeMap;
	std::unordered_map<BLOCK_TYPE, bool> saTypeMap;

	// 2. 
	// make cull face column bit: tp, sa
	// make column bit: ll, op
	// 0: x axis & left->right side (- => + : dir +)
	// 1: x axis & right->left side (+ => - : dir -)
	// 2: y axis & bottom->top side (- => + : dir +)
	// 3: y axis & top->bottom side (+ => - : dir -)
	// 4: z axis & front->back side (- => + : dir +)
	// 5: z axis & back->front side (+ => - : dir -)
	for (int x = 0; x < CHUNK_SIZE_P; ++x) {
		for (int y = 0; y < CHUNK_SIZE_P; ++y) {
			for (int z = 0; z < CHUNK_SIZE_P; ++z) {
				BLOCK_TYPE type = m_blocks[x][y][z].GetType();

				if (type == BLOCK_TYPE::BLOCK_AIR)
					continue;

				if (Block::IsTransparency(type)) {
					tpTypeMap[type] = true;

					// 주변이 타입이 다르고, 불투명 물체가 아닌 경우 페이스 존재
					if (x - 1 >= 0 && type != m_blocks[x - 1][y][z].GetType() &&
						!Block::IsOpaque(m_blocks[x - 1][y][z].GetType())) {
						memory->tpCullColBit[Utils::GetIndexFrom3D(0, y, z, CHUNK_SIZE_P)] |=
							(1ULL << x);
					}
					if (x + 1 < CHUNK_SIZE_P && type != m_blocks[x + 1][y][z].GetType() &&
						!Block::IsOpaque(m_blocks[x + 1][y][z].GetType())) {
						memory->tpCullColBit[Utils::GetIndexFrom3D(1, y, z, CHUNK_SIZE_P)] |=
							(1ULL << x);
					}

					if (y - 1 >= 0 && type != m_blocks[x][y - 1][z].GetType() &&
						!Block::IsOpaque(m_blocks[x][y - 1][z].GetType())) {
						memory->tpCullColBit[Utils::GetIndexFrom3D(2, z, x, CHUNK_SIZE_P)] |=
							(1ULL << y);
					}
					if (y + 1 < CHUNK_SIZE_P && type != m_blocks[x][y + 1][z].GetType() &&
						!Block::IsOpaque(m_blocks[x][y + 1][z].GetType())) {
						memory->tpCullColBit[Utils::GetIndexFrom3D(3, z, x, CHUNK_SIZE_P)] |=
							(1ULL << y);
					}

					if (z - 1 >= 0 && type != m_blocks[x][y][z - 1].GetType() &&
						!Block::IsOpaque(m_blocks[x][y][z - 1].GetType())) {
						memory->tpCullColBit[Utils::GetIndexFrom3D(4, y, x, CHUNK_SIZE_P)] |=
							(1ULL << z);
					}
					if (z + 1 < CHUNK_SIZE_P && type != m_blocks[x][y][z + 1].GetType() &&
						!Block::IsOpaque(m_blocks[x][y][z + 1].GetType())) {
						memory->tpCullColBit[Utils::GetIndexFrom3D(5, y, x, CHUNK_SIZE_P)] |=
							(1ULL << z);
					}
				}
				else if (Block::IsSemiAlpha(type)) {
					saTypeMap[type] = true;

					// - -> + : 불투명이 아니면 페이스 존재 -> 같은 타입을 고려하지 않음
					if (x + 1 < CHUNK_SIZE_P && !Block::IsOpaque(m_blocks[x + 1][y][z].GetType())) {
						memory->saCullColBit[Utils::GetIndexFrom3D(1, y, z, CHUNK_SIZE_P)] |=
							(1ULL << x);
					}
					if (y + 1 < CHUNK_SIZE_P && !Block::IsOpaque(m_blocks[x][y + 1][z].GetType())) {
						memory->saCullColBit[Utils::GetIndexFrom3D(3, z, x, CHUNK_SIZE_P)] |=
							(1ULL << y);
					}
					if (z + 1 < CHUNK_SIZE_P && !Block::IsOpaque(m_blocks[x][y][z + 1].GetType())) {
						memory->saCullColBit[Utils::GetIndexFrom3D(5, y, x, CHUNK_SIZE_P)] |=
							(1ULL << z);
					}

					// + -> - : 투명일 때만 페이스 존재 -> 같은 타입을 고려하지 않음
					if (x - 1 >= 0 && Block::IsTransparency(m_blocks[x - 1][y][z].GetType())) {
						memory->saCullColBit[Utils::GetIndexFrom3D(0, y, z, CHUNK_SIZE_P)] |=
							(1ULL << x);
					}
					if (y - 1 >= 0 && Block::IsTransparency(m_blocks[x][y - 1][z].GetType())) {
						memory->saCullColBit[Utils::GetIndexFrom3D(2, z, x, CHUNK_SIZE_P)] |=
							(1ULL << y);
					}
					if (z - 1 >= 0 && Block::IsTransparency(m_blocks[x][y][z - 1].GetType())) {
						memory->saCullColBit[Utils::GetIndexFrom3D(4, y, x, CHUNK_SIZE_P)] |=
							(1ULL << z);
					}

					llTypeMap[type] = true;
					memory->llColBit[Utils::GetIndexFrom3D(0, y, z, CHUNK_SIZE_P)] |= (1ULL << x);
					memory->llColBit[Utils::GetIndexFrom3D(1, z, x, CHUNK_SIZE_P)] |= (1ULL << y);
					memory->llColBit[Utils::GetIndexFrom3D(2, y, x, CHUNK_SIZE_P)] |= (1ULL << z);
				}
				else { // opaque
					opTypeMap[type] = true;
					memory->opColBit[Utils::GetIndexFrom3D(0, y, z, CHUNK_SIZE_P)] |= (1ULL << x);
					memory->opColBit[Utils::GetIndexFrom3D(1, z, x, CHUNK_SIZE_P)] |= (1ULL << y);
					memory->opColBit[Utils::GetIndexFrom3D(2, y, x, CHUNK_SIZE_P)] |= (1ULL << z);

					llTypeMap[type] = true;
					memory->llColBit[Utils::GetIndexFrom3D(0, y, z, CHUNK_SIZE_P)] |= (1ULL << x);
					memory->llColBit[Utils::GetIndexFrom3D(1, z, x, CHUNK_SIZE_P)] |= (1ULL << y);
					memory->llColBit[Utils::GetIndexFrom3D(2, y, x, CHUNK_SIZE_P)] |= (1ULL << z);
				}
			}
		}
	}


	// 3. face cull: lowlod & opaque 
	for (int axis = 0; axis < 3; ++axis) {
		for (int h = 1; h < CHUNK_SIZE_P - 1; ++h) {
			for (int w = 1; w < CHUNK_SIZE_P - 1; ++w) {
				uint64_t llBit = memory->llColBit[Utils::GetIndexFrom3D(axis, h, w, CHUNK_SIZE_P)];
				memory->llCullColBit[Utils::GetIndexFrom3D(axis * 2 + 0, h, w, CHUNK_SIZE_P)] =
					llBit & ~(llBit << 1);
				memory->llCullColBit[Utils::GetIndexFrom3D(axis * 2 + 1, h, w, CHUNK_SIZE_P)] =
					llBit & ~(llBit >> 1);

				uint64_t opBit = memory->opColBit[Utils::GetIndexFrom3D(axis, h, w, CHUNK_SIZE_P)];
				memory->opCullColBit[Utils::GetIndexFrom3D(axis * 2 + 0, h, w, CHUNK_SIZE_P)] =
					opBit & ~(opBit << 1);
				memory->opCullColBit[Utils::GetIndexFrom3D(axis * 2 + 1, h, w, CHUNK_SIZE_P)] =
					opBit & ~(opBit >> 1);
			}
		}
	}


	// 4. face cull column bit -> face slice column bit
	std::unordered_map<BLOCK_TYPE, std::vector<uint64_t>> llSliceColBit;
	std::unordered_map<BLOCK_TYPE, std::vector<uint64_t>> opSliceColBit;
	std::unordered_map<BLOCK_TYPE, std::vector<uint64_t>> tpSliceColBit;
	std::unordered_map<BLOCK_TYPE, std::vector<uint64_t>> saSliceColBit;

	MakeFaceSliceColumnBit(memory->llCullColBit, llSliceColBit);
	MakeFaceSliceColumnBit(memory->opCullColBit, opSliceColBit);
	MakeFaceSliceColumnBit(memory->tpCullColBit, tpSliceColBit);
	MakeFaceSliceColumnBit(memory->saCullColBit, saSliceColBit);


	// 5. make vertices by bit slices column (greedy meshing)
	ClearCpuVertices();
	for (const auto& t : llTypeMap) {
		if (llSliceColBit.find(t.first) != llSliceColBit.end()) {
			GreedyMeshing(llSliceColBit[t.first], m_lowLodVertices, m_lowLodIndices, t.first);
		}
	}

	for (const auto& t : opTypeMap) {
		if (opSliceColBit.find(t.first) != opSliceColBit.end()) {
			GreedyMeshing(opSliceColBit[t.first], m_opaqueVertices, m_opaqueIndices, t.first);
		}
	}

	for (const auto& t : tpTypeMap) {
		if (tpSliceColBit.find(t.first) != tpSliceColBit.end()) {
			GreedyMeshing(
				tpSliceColBit[t.first], m_transparencyVertices, m_transparencyIndices, t.first);
		}
	}

	for (const auto& t : saTypeMap) {
		if (saSliceColBit.find(t.first) != saSliceColBit.end()) {
			GreedyMeshing(saSliceColBit[t.first], m_semiAlphaVertices, m_semiAlphaIndices, t.first);
		}
	}
}

void Chunk::MakeFaceSliceColumnBit(uint64_t cullColBit[Chunk::CHUNK_SIZE_P2 * 6],
	std::unordered_map<BLOCK_TYPE, std::vector<uint64_t>>& sliceColBit)
{
	/*
	 *     ---------------
	 *    / x    y    z /| <= 5:face, h:1, w:2 bits
	 *   / d    e    f / |
	 *  | 4    5    6 |  |
	 *  |			  |  |	     xi   jy  zk (3rd z slice - => +)
	 *  |   i    j    |k /  =>   da   eb  fc (2nd z slice - => +)   => bitPos->slice, h->shift, w->w
	 *  |  a    b    c| /        41   52  63 (1st z slice - => +)
	 *  | 1    2    3 |/
	 *  ---------------
	 */

	for (uint8_t face = 0; face < 6; ++face) {
		for (int h = 0; h < CHUNK_SIZE; ++h) {
			for (int w = 0; w < CHUNK_SIZE; ++w) {
				uint64_t colbit = // 34bit: P,CHUNK_SIZE,P
					cullColBit[Utils::GetIndexFrom3D(face, h + 1, w + 1, CHUNK_SIZE_P)];
				colbit = colbit >> 1;					 // 33bit: P,CHUNK_SIZE
				colbit = colbit & ~(1ULL << CHUNK_SIZE); // 32bit: CHUNK_SIZE

				while (colbit) {
					// bitPos가 바라보는 방향의 Slice 면임
					int bitPos = Utils::TrailingZeros(colbit); // 1110001000 -> trailing zero : 3
					colbit = colbit & (colbit - 1ULL);		   // 1110000000

					BLOCK_TYPE type = BLOCK_TYPE::BLOCK_AIR;
					if (face <= DIR::RIGHT) { // left right
						type = m_blocks[bitPos + 1][h + 1][w + 1].GetType();
					}
					else if (face <= DIR::TOP) { // bottom top
						type = m_blocks[w + 1][bitPos + 1][h + 1].GetType();
					}
					else { //(face < 6) // front back
						type = m_blocks[w + 1][h + 1][bitPos + 1].GetType();
					}

					if (sliceColBit.find(type) == sliceColBit.end()) {
						sliceColBit[type] = std::vector<uint64_t>(Chunk::CHUNK_SIZE2 * 6, 0);
					}
					sliceColBit[type][Utils::GetIndexFrom3D(face, bitPos, w, CHUNK_SIZE)] |=
						(1ULL << h);
				}
			}
		}
	}
}

void Chunk::GreedyMeshing(std::vector<uint64_t>& faceColBit, std::vector<VoxelVertex>& vertices,
	std::vector<uint32_t>& indices, BLOCK_TYPE type)
{
	// face 0, 1 : left,right
	// face 2, 3 : bottom, top
	// face 4, 5 : front,back
	for (uint8_t face = 0; face < 6; ++face) {
		TEXTURE_INDEX textureIndex = (TEXTURE_INDEX)Block::GetBlockTextureIndex(type, face);

		for (int s = 0; s < CHUNK_SIZE; ++s) {
			for (int i = 0; i < CHUNK_SIZE; ++i) {
				uint64_t faceBit = faceColBit[Utils::GetIndexFrom3D(face, s, i, CHUNK_SIZE)];

				int step = 0;
				while (step < CHUNK_SIZE) {						   // 111100011100
					step += Utils::TrailingZeros(faceBit >> step); // 1111000111|00| -> 2
					if (step >= CHUNK_SIZE)
						break;

					int ones = Utils::TrailingOnes((faceBit >> step));	// 1111000|111|00 -> 3
					uint64_t submask = ((1ULL << ones) - 1ULL) << step; // 111 << 2 -> 11100

					int w = 1;
					while (i + w < CHUNK_SIZE) {
						uint64_t cb =
							faceColBit[Utils::GetIndexFrom3D(face, s, i + w, CHUNK_SIZE)] & submask;
						if (cb != submask)
							break;

						faceColBit[Utils::GetIndexFrom3D(face, s, i + w, CHUNK_SIZE)] &= (~submask);
						w++;
					}

					if (face == DIR::LEFT)
						MeshGenerator::CreateQuadMesh(
							vertices, indices, s, step, i, w, ones, face, textureIndex);
					else if (face == DIR::RIGHT)
						MeshGenerator::CreateQuadMesh(
							vertices, indices, s + 1, step, i, w, ones, face, textureIndex);
					else if (face == DIR::BOTTOM)
						MeshGenerator::CreateQuadMesh(
							vertices, indices, i, s, step, w, ones, face, textureIndex);
					else if (face == DIR::TOP)
						MeshGenerator::CreateQuadMesh(
							vertices, indices, i, s + 1, step, w, ones, face, textureIndex);
					else if (face == DIR::FRONT)
						MeshGenerator::CreateQuadMesh(
							vertices, indices, i, step, s, w, ones, face, textureIndex);
					else // face == DIR::BACK
						MeshGenerator::CreateQuadMesh(
							vertices, indices, i, step, s + 1, w, ones, face, textureIndex);

					step += ones;
				}
			}
		}
	}
}