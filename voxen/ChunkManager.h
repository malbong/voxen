#pragma once

#include <unordered_map>
#include <unordered_set>
#include <future>

#include "Chunk.h"
#include "Camera.h"
#include "Light.h"
#include "Instance.h"

using namespace DirectX;
using namespace DirectX::SimpleMath;

class ChunkManager {

public:
	static const int CHUNK_COUNT = 2 * (Camera::MAX_RENDER_DISTANCE / Chunk::CHUNK_SIZE) + 1;
	static const int MAX_HEIGHT = 256;
	static const int MAX_HEIGHT_CHUNK_COUNT = 8;
	static const int CHUNK_COUNT_P = CHUNK_COUNT + 2;
	static const int CHUNK_COUNT2 = CHUNK_COUNT * CHUNK_COUNT;
	static const int MAX_HEIGHT_CHUNK_COUNT_P = MAX_HEIGHT_CHUNK_COUNT + 2;
	static const int CHUNK_POOL_SIZE = CHUNK_COUNT_P * CHUNK_COUNT_P * MAX_HEIGHT_CHUNK_COUNT_P;
	static const int MAX_INSTANCE_BUFFER_SIZE = 1024 * 1024 * 8;
	static const int MAX_INSTANCE_BUFFER_COUNT =
		MAX_INSTANCE_BUFFER_SIZE / sizeof(InstanceInfoVertex);

	static ChunkManager* GetInstance();

	bool Initialize(Vector3 cameraChunkPos);
	void Update(float dt, Camera& camera, Light& light, bool mouseLeftDown, bool mouseRightDown);

	void RenderOpaqueChunk(Chunk* chunk);
	void RenderSemiAlphaChunk(Chunk* chunk);
	void RenderLowLodChunk(Chunk* chunk);
	void RenderTransparencyChunk(Chunk* chunk);
	void RenderInstance();

	void RenderBasic(Vector3 cameraPos);
	void RenderMirrorWorld();
	void RenderTransparency();
	void RenderBasicShadowMap();

	const Chunk* GetChunkByPosition(Vector3 position);
	const Block* GetBlockByPosition(Vector3 position);
	const Instance* GetInstanceByPosition(Vector3 position);

	bool HasObjectAt(Vector3 position);
	void RemoveBlockPatchAt(Vector3 position);
	void AddBlockPatchAt(Vector3 position, DIR face);

	PatchData MakePatchData(int x, int y, int z, BLOCK_TYPE blockType, int baseSize, bool needWrap);
	PatchData MakePatchData(Vector3 position, BLOCK_TYPE blockType, int baseSize, bool needWrap);
	void GenerateEdgePatchEntry(int x, int y, int z, Vector3 chunkPosition, BLOCK_TYPE blockType,
		std::pair<PosInt3, PatchData>* outEdgePatchEntry, int& outEdgePatchEntryCount);
	void GenerateEdgePatchEntry(Vector3 position, Vector3 chunkPosition, BLOCK_TYPE blockType,
		std::pair<PosInt3, PatchData>* outEdgePatchEntry, int& outEdgePatchEntryCount);
	

private:
	static ChunkManager* chunkManager;

	ChunkManager();
	~ChunkManager();
	ChunkManager(const ChunkManager& other);
	void operator=(const ChunkManager& rhs);

	void UpdateChunkList(Vector3 cameraChunkPos);
	void UpdateLoadChunkList(Camera& camera);
	void UpdateUnloadChunkList();
	void UpdatePatchChunkMap(Camera& camera);
	void UpdateRenderChunkList(Camera& camera, Light& light);
	void UpdateInstanceInfoList(Camera& camera);
	void UpdateChunkConstant(float dt);

	bool FrustumCulling(
		Vector3 position, Camera& camera, Light& light, bool useMirror, bool useShadow, int index = 0);

	void UpdateChunkBuffer(Chunk* chunk);
	
	Chunk* GetChunkFromPool();
	void ReleaseChunkToPool(Chunk* chunk);

	bool MakeInstanceVertexBuffer();
	bool MakeInstanceInfoBuffer();

	std::vector<Chunk*> m_chunkPool;
	PosHashMap<Chunk*> m_chunkMap;

	PosHashMap<PosHashMap<PatchDataHashSet>> m_patchDependencyMap;
	PosHashMap<PosHashSet> m_lookupDependencySet;
	PosHashMap<PosHashSet> m_patchedChunkSet;
	PosHashMap<PatchDataHashSet> m_cameraPatchChunkMap;
	PosHashMap<PatchDataHashSet> m_patchChunkMap;

	std::vector<Chunk*> m_loadChunkList;
	std::vector<Chunk*> m_unloadChunkList;
	std::vector<Chunk*> m_renderChunkList;
	std::vector<Chunk*> m_renderMirrorChunkList;
	std::vector<Chunk*> m_renderShadowChunkList;

	std::vector<ComPtr<ID3D11Buffer>> m_lowLodVertexBuffers;
	std::vector<ComPtr<ID3D11Buffer>> m_lowLodIndexBuffers;

	std::vector<ComPtr<ID3D11Buffer>> m_opaqueVertexBuffers;
	std::vector<ComPtr<ID3D11Buffer>> m_opaqueIndexBuffers;

	std::vector<ComPtr<ID3D11Buffer>> m_transparencyVertexBuffers;
	std::vector<ComPtr<ID3D11Buffer>> m_transparencyIndexBuffers;

	std::vector<ComPtr<ID3D11Buffer>> m_semiAlphaVertexBuffers;
	std::vector<ComPtr<ID3D11Buffer>> m_semiAlphaIndexBuffers;

	std::vector<ComPtr<ID3D11Buffer>> m_constantBuffers;

	std::vector<ComPtr<ID3D11Buffer>> m_instanceVertexBuffers;
	std::vector<ComPtr<ID3D11Buffer>> m_instanceIndexBuffers;
	std::vector<ComPtr<ID3D11Buffer>> m_instanceInfoBuffers;
	std::vector<std::vector<InstanceInfoVertex>> m_instanceInfoList;
	std::vector<UINT> m_instanceIndexCount;
	
	std::vector<ChunkLoadMemory*> m_chunkLoadMemoryPool;

	uint32_t m_initThreadCount;
	std::vector<std::pair<Chunk*, std::future<ChunkLoadMemory*>>> m_initFutures;

	uint32_t m_patchThreadCount;
	std::vector<std::pair<Chunk*, std::future<ChunkLoadMemory*>>> m_patchFutures;
};

