#pragma once

#include "Structure.h"
#include "Terrain.h"
#include "Biome.h"
#include "PatchData.h"

#include <d3d11.h>
#include <wrl.h>
#include <directxtk/SimpleMath.h>
#include <vector>

using namespace Microsoft::WRL;
using namespace DirectX::SimpleMath;

struct ChunkPatchData;
struct ChunkLoadMemory;

class Chunk {

public:
	static const int CHUNK_SIZE = 32;
	static const int CHUNK_SIZE2 = CHUNK_SIZE * CHUNK_SIZE;
	static const int CHUNK_SIZE_P = CHUNK_SIZE + 2;
	static const int CHUNK_SIZE_P2 = CHUNK_SIZE_P * CHUNK_SIZE_P;

	static const uint32_t TREE_PLACE_RANDOM_SOLT_X = 763777711U;
	static const uint32_t TREE_PLACE_RANDOM_SOLT_Z = 128200883U;
	static const uint32_t TREE_PLACE_MAX_COUNT_PER_CHUNK = Chunk::CHUNK_SIZE2 / 16;

	static const uint32_t INSTANCE_PLACE_RANDOM_SOLT_X = 405071179U;
	static const uint32_t INSTANCE_PLACE_RANDOM_SOLT_Z = 397760329U;
	static const uint32_t INSTANCE_PLACE_MAX_COUNT_PER_CHUNK = Chunk::CHUNK_SIZE2 / 4;

	Chunk(UINT id);
	~Chunk();

	ChunkLoadMemory* Initialize(ChunkLoadMemory* memory);
	ChunkLoadMemory* Patch(const PatchDataHashSet& patchDataSet, ChunkLoadMemory* memory);
	void Update(float dt);
	void Clear();
	void UpdateCpuBufferCount();

	inline UINT GetID() { return m_id; }

	inline void SetLoad(bool isLoaded) { m_isLoaded = isLoaded; }
	inline bool IsLoaded() const { return m_isLoaded; }

	inline void SetIsPatching(bool isPatching) { m_isPatching = isPatching; }
	inline bool IsPatching() const { return m_isPatching; }

	inline void SetUpdateRequired(bool isRequired) { m_isUpdateRequired = isRequired; }
	inline bool IsUpdateRequired() const { return m_isUpdateRequired; }

	inline bool OnPatchDirtyFlag() { return m_onPatchDirtyFlag; }

	inline Vector3 GetOffsetPosition() const { return m_offsetPosition; }
	inline void SetOffsetPosition(Vector3 offsetPosition) { m_offsetPosition = offsetPosition; }
	inline Vector3 GetPosition() const { return m_position; }

	inline bool IsEmpty() const
	{
		return IsEmptyOpaque() && IsEmptyTransparency() && IsEmptySemiAlpha();
	}
	inline bool IsEmptyLowLod() const { return m_lowLodVertexCount == 0; }
	inline bool IsEmptyOpaque() const { return m_opaqueVertexCount == 0; }
	inline bool IsEmptyTransparency() const { return m_transparencyVertexCount == 0; }
	inline bool IsEmptySemiAlpha() const { return m_semiAlphaVertexCount == 0; }
	inline bool IsEmptyInstance() const { return m_instanceMap.empty(); }

	inline const std::vector<VoxelVertex>& GetLowLodVertices() const { return m_lowLodVertices; }
	inline const std::vector<uint32_t>& GetLowLodIndices() const { return m_lowLodIndices; }
	inline const std::vector<VoxelVertex>& GetOpaqueVertices() const { return m_opaqueVertices; }
	inline const std::vector<uint32_t>& GetOpaqueIndices() const { return m_opaqueIndices; }
	inline const std::vector<VoxelVertex>& GetTransparencyVertices() const
	{
		return m_transparencyVertices;
	}
	inline const std::vector<uint32_t>& GetTransparencyIndices() const
	{
		return m_transparencyIndices;
	}

	inline const std::vector<VoxelVertex>& GetSemiAlphaVertices() const
	{
		return m_semiAlphaVertices;
	}
	inline const std::vector<uint32_t>& GetSemiAlphaIndices() const { return m_semiAlphaIndices; }

	inline const PosHashMap<Instance>& GetInstanceMap() const { return m_instanceMap; }

	inline uint32_t GetLowLodVertexCount() const { return m_lowLodVertexCount; }
	inline uint32_t GetLowLodIndexCount() const { return m_lowLodIndexCount; }
	inline uint32_t GetOpaqueVertexCount() const { return m_opaqueVertexCount; }
	inline uint32_t GetOpaqueIndexCount() const { return m_opaqueIndexCount; }
	inline uint32_t GetTransparencyVertexCount() const { return m_transparencyVertexCount; }
	inline uint32_t GetTransparencyIndexCount() const { return m_transparencyIndexCount; }
	inline uint32_t GetSemiAlphaVertexCount() const { return m_semiAlphaVertexCount; }
	inline uint32_t GetSemiAlphaIndexCount() const { return m_semiAlphaIndexCount; }

	inline const ChunkConstantData& GetConstantData() const { return m_constantData; }

	inline const Block* GetBlock(Vector3 pos) const
	{
		return &m_blocks[Utils::WrapToBase((int)std::floor(pos.x), CHUNK_SIZE) + 1]
						[Utils::WrapToBase((int)std::floor(pos.y), CHUNK_SIZE) + 1]
						[Utils::WrapToBase((int)std::floor(pos.z), CHUNK_SIZE) + 1];
	}

	inline const Instance* GetInstance(Vector3 pos) const
	{
		auto iter =
			m_instanceMap.find(PosInt3(Utils::WrapToBase((int)std::floor(pos.x), CHUNK_SIZE),
				Utils::WrapToBase((int)std::floor(pos.y), CHUNK_SIZE),
				Utils::WrapToBase((int)std::floor(pos.z), CHUNK_SIZE)));

		if (iter == m_instanceMap.end())
			return nullptr;
		else
			return &(iter->second);
	}


private:
	void ClearCpuVertices();

	void InitTerrainNoises(ChunkLoadMemory* memory);

	void InitBiomeMapAndCount(ChunkLoadMemory* memory);

	void InitBasicBlockType(ChunkLoadMemory* memory);

	uint32_t GetMaxPlaceCountByBiomeRatio(
		BIOME_TYPE biomeType, int maxCountPerChunk, int biomeCount);

	void InitTreePlace(ChunkLoadMemory* memory);
	bool CanPlaceTreeAt(
		int x, int y, int z, uint32_t placedBiomeTreeCount, ChunkLoadMemory* memory);
	bool CheckTreePlaceCondition(int x, int y, int z);
	void PlaceTree(int x, int y, int z, ChunkLoadMemory* memory, TREE_TYPE treeType);
	bool IsInsideChunk(int x, int y, int z);
	bool IsInsideChunkWithPadding(int x, int y, int z);
	bool IsInnerEdge(int x, int y, int z);
	bool IsOuterEdge(int x, int y, int z);
	
	void InitInstancePlace(ChunkLoadMemory* memory);
	bool IsInstanceAt(int x, int y, int z);
	INSTANCE_TYPE GetWaterPlaneInstanceType(int x, int z, ChunkLoadMemory* memory);
	bool CanPlaceWaterPlaneInstanceAt(int x, int z, ChunkLoadMemory* memory);
	void SetWaterPlaneInstance(int x, int z, INSTANCE_TYPE instanceType, ChunkLoadMemory* memory);
	INSTANCE_TYPE GetBiomeInstanceType(int x, int y, int z, ChunkLoadMemory* memory);
	bool CanPlaceBiomeInstanceAt(
		int x, int y, int z, uint32_t placedBiomeInstanceCount, ChunkLoadMemory* memory);
	bool CheckInstancePlaceCondition(INSTANCE_TYPE type, int x, int y, int z);
	void SetBiomeInstance(int x, int y, int z, INSTANCE_TYPE instanceType, ChunkLoadMemory* memory);

	void InitWorldVerticesData(ChunkLoadMemory* memory);
	void MakeFaceSliceColumnBit(uint64_t cullColBit[Chunk::CHUNK_SIZE_P2 * 6],
		std::unordered_map<BLOCK_TYPE, std::vector<uint64_t>>& sliceColBit);
	void GreedyMeshing(std::vector<uint64_t>& faceColBit, std::vector<VoxelVertex>& vertices,
		std::vector<uint32_t>& indices, BLOCK_TYPE types);

	Block m_blocks[CHUNK_SIZE_P][CHUNK_SIZE_P][CHUNK_SIZE_P];
	PosHashMap<Instance> m_instanceMap;

	UINT m_id;
	bool m_isLoaded;
	bool m_isPatching;
	bool m_isUpdateRequired;
	bool m_onPatchDirtyFlag;

	Vector3 m_offsetPosition;
	Vector3 m_position;

	std::vector<VoxelVertex> m_lowLodVertices;
	std::vector<uint32_t> m_lowLodIndices;
	uint32_t m_lowLodVertexCount;
	uint32_t m_lowLodIndexCount;

	std::vector<VoxelVertex> m_opaqueVertices;
	std::vector<uint32_t> m_opaqueIndices;
	uint32_t m_opaqueVertexCount;
	uint32_t m_opaqueIndexCount;

	std::vector<VoxelVertex> m_transparencyVertices;
	std::vector<uint32_t> m_transparencyIndices;
	uint32_t m_transparencyVertexCount;
	uint32_t m_transparencyIndexCount;

	std::vector<VoxelVertex> m_semiAlphaVertices;
	std::vector<uint32_t> m_semiAlphaIndices;
	uint32_t m_semiAlphaVertexCount;
	uint32_t m_semiAlphaIndexCount;

	ChunkConstantData m_constantData;
};

struct ChunkLoadMemory {
	uint64_t llColBit[Chunk::CHUNK_SIZE_P2 * 3];
	uint64_t opColBit[Chunk::CHUNK_SIZE_P2 * 3];

	uint64_t llCullColBit[Chunk::CHUNK_SIZE_P2 * 6];
	uint64_t opCullColBit[Chunk::CHUNK_SIZE_P2 * 6];
	uint64_t tpCullColBit[Chunk::CHUNK_SIZE_P2 * 6];
	uint64_t saCullColBit[Chunk::CHUNK_SIZE_P2 * 6];

	float continentalinessNoises[Chunk::CHUNK_SIZE_P][Chunk::CHUNK_SIZE_P];
	float erosionNoises[Chunk::CHUNK_SIZE_P][Chunk::CHUNK_SIZE_P];
	float peaksValleyNoises[Chunk::CHUNK_SIZE_P][Chunk::CHUNK_SIZE_P];
	float temperatureNoises[Chunk::CHUNK_SIZE_P][Chunk::CHUNK_SIZE_P];
	float humidityNoises[Chunk::CHUNK_SIZE_P][Chunk::CHUNK_SIZE_P];
	float distributionNoises[Chunk::CHUNK_SIZE_P][Chunk::CHUNK_SIZE_P];
	float elevationNoises[Chunk::CHUNK_SIZE_P][Chunk::CHUNK_SIZE_P];

	BIOME_TYPE biomeMap2D[Chunk::CHUNK_SIZE][Chunk::CHUNK_SIZE];
	uint32_t biomeCount[Biome::BIOME_TYPE_COUNT];

	std::vector<std::pair<int, int>> treeRandomPlace2D;
	std::vector<std::pair<int, int>> instanceRandomPlace2D;

	PosHashMap<PatchDataHashSet> chunkPatchDataMap;

	ChunkLoadMemory()
		: llColBit{ 0 }, opColBit{ 0 }, llCullColBit{ 0 }, opCullColBit{ 0 }, tpCullColBit{ 0 },
		  saCullColBit{ 0 },
		  continentalinessNoises{
			  {
				  0,
			  },
		  },
		  erosionNoises{
			  {
				  0,
			  },
		  },
		  peaksValleyNoises{
			  {
				  0,
			  },
		  },
		  temperatureNoises{
			  {
				  0,
			  },
		  },
		  humidityNoises{
			  {
				  0,
			  },
		  },
		  distributionNoises{
			  {
				  0,
			  },
		  },
		  elevationNoises{
			  {
				  0,
			  },
		  },
		  biomeMap2D{
			  {
				  (BIOME_TYPE)0,
			  },
		  },
		  biomeCount{
			  0,
		  }
	{
		instanceRandomPlace2D.reserve(Chunk::INSTANCE_PLACE_MAX_COUNT_PER_CHUNK);
		treeRandomPlace2D.reserve(Chunk::TREE_PLACE_MAX_COUNT_PER_CHUNK);
	}

	void Clear()
	{
		std::fill(std::begin(llColBit), std::end(llColBit), 0);
		std::fill(std::begin(opColBit), std::end(opColBit), 0);

		std::fill(std::begin(llCullColBit), std::end(llCullColBit), 0);
		std::fill(std::begin(opCullColBit), std::end(opCullColBit), 0);
		std::fill(std::begin(tpCullColBit), std::end(tpCullColBit), 0);
		std::fill(std::begin(saCullColBit), std::end(saCullColBit), 0);

		std::fill(std::begin(biomeCount), std::end(biomeCount), 0);

		treeRandomPlace2D.clear();
		instanceRandomPlace2D.clear();
		chunkPatchDataMap.clear();
	}
};