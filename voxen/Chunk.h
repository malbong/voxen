#pragma once

#include "Block.h"
#include "Instance.h"
#include "Structure.h"
#include "Terrain.h"

#include <d3d11.h>
#include <wrl.h>
#include <directxtk/SimpleMath.h>
#include <vector>
#include <map>

using namespace Microsoft::WRL;
using namespace DirectX::SimpleMath;

struct ChunkInitMemory;

class Chunk {

public:
	static const int CHUNK_SIZE = 32;
	static const int CHUNK_SIZE2 = CHUNK_SIZE * CHUNK_SIZE;
	static const int CHUNK_SIZE_P = CHUNK_SIZE + 2;
	static const int CHUNK_SIZE_P2 = CHUNK_SIZE_P * CHUNK_SIZE_P;

	Chunk(UINT id);
	~Chunk();

	ChunkInitMemory* Initialize(ChunkInitMemory* memory);
	void Update(float dt);
	void Clear();

	inline UINT GetID() { return m_id; }

	inline void SetLoad(bool isLoaded) { m_isLoaded = isLoaded; }
	inline bool IsLoaded() const { return m_isLoaded; }
	inline bool IsEmpty() const { return IsEmptyOpaque() && IsEmptyTransparency() && IsEmptySemiAlpha(); }

	inline Vector3 GetOffsetPosition() const { return m_offsetPosition; }
	inline void SetOffsetPosition(Vector3 offsetPosition) { m_offsetPosition = offsetPosition; }
	inline Vector3 GetPosition() const { return m_position; }
	inline void SetUpdateRequired(bool isRequired) { m_isUpdateRequired = isRequired; }
	inline bool IsUpdateRequired() const { return m_isUpdateRequired; }

	inline bool IsEmptyLowLod() const { return m_lowLodVertices.empty(); }
	inline bool IsEmptyOpaque() const { return m_opaqueVertices.empty(); }
	inline bool IsEmptyTransparency() const { return m_transparencyVertices.empty(); }
	inline bool IsEmptySemiAlpha() const { return m_semiAlphaVertices.empty(); }

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

	inline const std::vector<Instance>& GetInstanceList() const
	{
		return m_instanceList;
	}

	inline const ChunkConstantData& GetConstantData() const { return m_constantData; }

	inline const Block* GetBlock(Vector3 pos) const
	{
		return &m_blocks[(int)std::floor(pos.x) % CHUNK_SIZE + 1]
						[(int)std::floor(pos.y) % CHUNK_SIZE + 1]
						[(int)std::floor(pos.z) % CHUNK_SIZE + 1];
	}

private:
	void InitChunkData();
	void InitInstanceInfoData();
	void InitWorldVerticesData(ChunkInitMemory* memory);

	void MakeFaceSliceColumnBit(uint64_t cullColBit[Chunk::CHUNK_SIZE_P2 * 6],
		std::unordered_map<BLOCK_TYPE, std::vector<uint64_t>>& sliceColBit);
	void GreedyMeshing(std::vector<uint64_t>& faceColBit, std::vector<VoxelVertex>& vertices,
		std::vector<uint32_t>& indices, BLOCK_TYPE types);

	Block m_blocks[CHUNK_SIZE_P][CHUNK_SIZE_P][CHUNK_SIZE_P];
	std::vector<Instance> m_instanceList;

	UINT m_id;
	bool m_isLoaded;
	bool m_isUpdateRequired;
	Vector3 m_offsetPosition;
	Vector3 m_position;

	std::vector<VoxelVertex> m_lowLodVertices;
	std::vector<uint32_t> m_lowLodIndices;

	std::vector<VoxelVertex> m_opaqueVertices;
	std::vector<uint32_t> m_opaqueIndices;

	std::vector<VoxelVertex> m_transparencyVertices;
	std::vector<uint32_t> m_transparencyIndices;

	std::vector<VoxelVertex> m_semiAlphaVertices;
	std::vector<uint32_t> m_semiAlphaIndices;

	ChunkConstantData m_constantData;
};

struct ChunkInitMemory {
	uint64_t llColBit[Chunk::CHUNK_SIZE_P2 * 3];
	uint64_t opColBit[Chunk::CHUNK_SIZE_P2 * 3];

	uint64_t llCullColBit[Chunk::CHUNK_SIZE_P2 * 6];
	uint64_t opCullColBit[Chunk::CHUNK_SIZE_P2 * 6];
	uint64_t tpCullColBit[Chunk::CHUNK_SIZE_P2 * 6];
	uint64_t saCullColBit[Chunk::CHUNK_SIZE_P2 * 6];

	ChunkInitMemory()
		: llColBit{ 0 }, opColBit{ 0 }, llCullColBit{ 0 }, opCullColBit{ 0 }, tpCullColBit{ 0 },
		  saCullColBit{ 0 }
	{
	}

	void Clear()
	{
		std::fill(std::begin(llColBit), std::end(llColBit), 0);
		std::fill(std::begin(opColBit), std::end(opColBit), 0);

		std::fill(std::begin(llCullColBit), std::end(llCullColBit), 0);
		std::fill(std::begin(opCullColBit), std::end(opCullColBit), 0);
		std::fill(std::begin(tpCullColBit), std::end(tpCullColBit), 0);
		std::fill(std::begin(saCullColBit), std::end(saCullColBit), 0);
	}
};