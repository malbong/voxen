#include "ChunkManager.h"
#include "Graphics.h"
#include "Utils.h"
#include "DXUtils.h"
#include "MeshGenerator.h"
#include "Block.h"
#include "Instance.h"

#include <iostream>
#include <algorithm>

ChunkManager* ChunkManager::GetInstance()
{
	static ChunkManager chunkManager;

	return &chunkManager;
}

ChunkManager::~ChunkManager()
{
	for (auto& [chunk, future] : m_patchFutures) {
		ChunkLoadMemory* mem = future.get();

		ReleaseChunkLoadMemoryToPool(mem);
	}

	for (auto& [chunk, future] : m_initFutures) {
		ChunkLoadMemory* mem = future.get();
		
		ReleaseChunkLoadMemoryToPool(mem);
		ReleaseChunkToPool(chunk);
	}

	for (auto& [pos, chunk] : m_chunkMap) {
		ReleaseChunkToPool(chunk);
	}
		
	for (ChunkLoadMemory* mem : m_chunkLoadMemoryPool)
		delete mem;

	// std::cout << m_chunkPool.size() << std::endl;
	// m_chunkPool == CHUNK_POOL_SIZE
	for (Chunk* chunk : m_chunkPool)
		delete chunk;
}

bool ChunkManager::Initialize(Vector3 cameraChunkPos)
{
	m_isOnChunkUpdateDirtyFlag = false;

	InitWorkerThreadCount();
	InitChunkLoadMemoryPool();
	InitChunkPool();

	m_lowLodVertexBuffers.resize(CHUNK_POOL_SIZE, nullptr);
	m_lowLodIndexBuffers.resize(CHUNK_POOL_SIZE, nullptr);

	m_opaqueVertexBuffers.resize(CHUNK_POOL_SIZE, nullptr);
	m_opaqueIndexBuffers.resize(CHUNK_POOL_SIZE, nullptr);

	m_transparencyVertexBuffers.resize(CHUNK_POOL_SIZE, nullptr);
	m_transparencyIndexBuffers.resize(CHUNK_POOL_SIZE, nullptr);

	m_semiAlphaVertexBuffers.resize(CHUNK_POOL_SIZE, nullptr);
	m_semiAlphaIndexBuffers.resize(CHUNK_POOL_SIZE, nullptr);

	m_constantBuffers.resize(CHUNK_POOL_SIZE, nullptr);

	m_instanceVertexBuffers.resize(INSTANCE_SHAPE::INSTANCE_SHAPE_COUNT, nullptr);
	m_instanceIndexBuffers.resize(INSTANCE_SHAPE::INSTANCE_SHAPE_COUNT, nullptr);
	m_instanceInfoBuffers.resize(INSTANCE_SHAPE::INSTANCE_SHAPE_COUNT, nullptr);
	m_instanceInfoList.resize(INSTANCE_SHAPE::INSTANCE_SHAPE_COUNT);
	if (!MakeInstanceVertexBuffer())
		return false;
	if (!MakeInstanceInfoBuffer())
		return false;

	UpdateLoadUnLoadChunkList(cameraChunkPos);

	return true;
}

void ChunkManager::RenderOpaqueChunk(Chunk* chunk)
{
	if (chunk->IsEmptyOpaque())
		return;

	UINT id = chunk->GetID();
	UINT stride = sizeof(VoxelVertex);
	UINT offset = 0;

	Graphics::context->IASetIndexBuffer(m_opaqueIndexBuffers[id].Get(), DXGI_FORMAT_R32_UINT, 0);
	Graphics::context->IASetVertexBuffers(
		0, 1, m_opaqueVertexBuffers[id].GetAddressOf(), &stride, &offset);
	Graphics::context->VSSetConstantBuffers(0, 1, m_constantBuffers[id].GetAddressOf());

	Graphics::context->DrawIndexed(chunk->GetOpaqueIndexCount(), 0, 0);
}

void ChunkManager::RenderSemiAlphaChunk(Chunk* chunk)
{
	if (chunk->IsEmptySemiAlpha())
		return;

	UINT id = chunk->GetID();
	UINT stride = sizeof(VoxelVertex);
	UINT offset = 0;

	Graphics::context->IASetIndexBuffer(m_semiAlphaIndexBuffers[id].Get(), DXGI_FORMAT_R32_UINT, 0);
	Graphics::context->IASetVertexBuffers(
		0, 1, m_semiAlphaVertexBuffers[id].GetAddressOf(), &stride, &offset);
	Graphics::context->VSSetConstantBuffers(0, 1, m_constantBuffers[id].GetAddressOf());

	Graphics::context->DrawIndexed(chunk->GetSemiAlphaIndexCount(), 0, 0);
}

void ChunkManager::RenderLowLodChunk(Chunk* chunk)
{
	if (chunk->IsEmptyLowLod())
		return;

	UINT id = chunk->GetID();
	UINT stride = sizeof(VoxelVertex);
	UINT offset = 0;

	Graphics::context->IASetIndexBuffer(m_lowLodIndexBuffers[id].Get(), DXGI_FORMAT_R32_UINT, 0);
	Graphics::context->IASetVertexBuffers(
		0, 1, m_lowLodVertexBuffers[id].GetAddressOf(), &stride, &offset);
	Graphics::context->VSSetConstantBuffers(0, 1, m_constantBuffers[id].GetAddressOf());

	Graphics::context->DrawIndexed(chunk->GetLowLodIndexCount(), 0, 0);
}

void ChunkManager::RenderTransparencyChunk(Chunk* chunk)
{
	if (chunk->IsEmptyTransparency())
		return;

	UINT id = chunk->GetID();
	UINT stride = sizeof(VoxelVertex);
	UINT offset = 0;

	Graphics::context->IASetIndexBuffer(
		m_transparencyIndexBuffers[id].Get(), DXGI_FORMAT_R32_UINT, 0);
	Graphics::context->IASetVertexBuffers(
		0, 1, m_transparencyVertexBuffers[id].GetAddressOf(), &stride, &offset);
	Graphics::context->VSSetConstantBuffers(0, 1, m_constantBuffers[id].GetAddressOf());

	Graphics::context->DrawIndexed(chunk->GetTransparencyIndexCount(), 0, 0);
}

void ChunkManager::RenderInstance()
{
	UINT indexCountPerInstance[INSTANCE_SHAPE::INSTANCE_SHAPE_COUNT] = { 12, 24, 6, 6 };

	for (int i = 0; i < INSTANCE_SHAPE::INSTANCE_SHAPE_COUNT; ++i) {
		Graphics::context->IASetIndexBuffer(
			m_instanceIndexBuffers[i].Get(), DXGI_FORMAT_R32_UINT, 0);

		std::vector<UINT> strides = { sizeof(InstanceVertex), sizeof(InstanceInfoVertex) };
		std::vector<UINT> offsets = { 0, 0 };
		std::vector<ID3D11Buffer*> buffers = { m_instanceVertexBuffers[i].Get(),
			m_instanceInfoBuffers[i].Get() };
		Graphics::context->IASetVertexBuffers(
			0, (UINT)buffers.size(), buffers.data(), strides.data(), offsets.data());
		Graphics::context->DrawIndexedInstanced(
			indexCountPerInstance[i], (UINT)m_instanceInfoList[i].size(), 0, 0, 0);
	}
}

void ChunkManager::RenderBasic(Vector3 cameraPos, bool useWireFrame)
{
	for (auto& c : m_renderChunkList) {
		Vector3 chunkOffset = c->GetOffsetPosition();
		Vector3 chunkCenterPosition = chunkOffset + Vector3(Chunk::CHUNK_SIZE * 0.5);
		Vector3 diffPosition = chunkCenterPosition - cameraPos;

		Graphics::SetPipelineStates(useWireFrame ? Graphics::basicWirePSO : Graphics::basicPSO);
		if (diffPosition.Length() > (float)Camera::LOD_RENDER_DISTANCE) {
			RenderLowLodChunk(c);
		}
		else {
			RenderOpaqueChunk(c);

			Graphics::SetPipelineStates(
				useWireFrame ? Graphics::semiAlphaWirePSO : Graphics::semiAlphaPSO);
			RenderSemiAlphaChunk(c);
		}
	}

	Graphics::SetPipelineStates(useWireFrame ? Graphics::instanceWirePSO : Graphics::instancePSO);
	RenderInstance();
}

void ChunkManager::RenderMirrorWorld()
{
	Graphics::SetPipelineStates(Graphics::basicMirrorPSO);
	for (auto& c : m_renderMirrorChunkList) {
		RenderLowLodChunk(c);
	}

	Graphics::SetPipelineStates(Graphics::instanceMirrorPSO);
	RenderInstance();
}

void ChunkManager::RenderTransparency()
{
	for (auto& c : m_renderChunkList) {
		RenderTransparencyChunk(c);
	}
}

void ChunkManager::RenderBasicShadowMap()
{
	for (auto& c : m_renderShadowChunkList)
		RenderLowLodChunk(c);
}

void ChunkManager::RenderBasicAlbedo()
{
	Graphics::SetPipelineStates(Graphics::basicAlbedoPSO);
	for (auto& c : m_renderChunkList) {
		RenderLowLodChunk(c);
	}
}

void ChunkManager::Update(float dt, Camera& camera, const Light& light)
{
	/*
	 * 청크 위치를 벗어난 경우 Load, Unload 리스트를 재구성
	 */
	if (m_isOnChunkUpdateDirtyFlag) {
		UpdateLoadUnLoadChunkList(camera.GetChunkPosition());

		m_isOnChunkUpdateDirtyFlag = false;
	}

	/*
	 * Load, Unload, Patch는 매 프레임 실행
	 * List나 Map으로 관리하여 매 프레임 실행하게 될 것
	 * Load, Patch의 경우 멀티쓰레드 환경이므로 로드가 완료 시 동기화함
	 */
	LoadChunks(camera);
	SyncLoadedChunks();

	UnloadChunks();

	PatchChunks(camera);
	SyncPatchedChunks();

	/*
	 * Load된 청크들을 순회하여 실질적으로 사용하게 될 List 재구성
	 * - RenderChunkList: Frustum Culling 진행
	 * - InstanceInfoList: Instance Rendering을 위한 InfoBuffer 구성
	 */
	UpdateRenderChunkList(camera, light);
	UpdateInstanceInfoList(camera);

	/*
	 * 청크 자체 Update를 진행
	 */
	UpdateChunkConstant(dt);
}

void ChunkManager::UpdateLoadUnLoadChunkList(Vector3 cameraChunkPos)
{
	/*
	 * m_renderablePosMap
	 * - 카메라 기준 렌더링해야 할 청크들의 포지션 3D Grid Map
	 * - 청크 로드 후 사용 가능한 청크로 동기화할 때 재검사를 위한 3D Grid Map임
	 * - 실제 청크를 담지 않고 월드 좌표 위치를 튜플로 저장해놓음
	 */
	m_renderablePosMap.clear();
	for (int y = 0; y < MAX_HEIGHT_CHUNK_COUNT; ++y) {
		for (int x = 0; x < CHUNK_COUNT; ++x) {
			for (int z = 0; z < CHUNK_COUNT; ++z) {
				int worldY = Chunk::CHUNK_SIZE * y;
				int worldX = (int)cameraChunkPos.x + Chunk::CHUNK_SIZE * (x - CHUNK_COUNT / 2);
				int worldZ = (int)cameraChunkPos.z + Chunk::CHUNK_SIZE * (z - CHUNK_COUNT / 2);

				PosInt3 worldPos(worldX, worldY, worldZ);
				m_renderablePosMap[worldPos] = true;
			}
		}
	}

	/*
	 * m_waitLoadChunkPosMap
	 * - 로드를 대기할 청크들의 위치를 담고 있는 3D Grid Map
	 * - 청크를 로드에 넣으면 해당 Map에서 제거되고, 모든 청크가 로드될때까지 해당 map에 존재하게 됨
	 * - 로드 쓰레드의 개수 한계로 인해 관리를 잘 해야 함
	 *
	 * 렌더링가능한 위치를 순회하여 chunkMap에 존재하는지 판단
	 * - 없으면 로드를 대기할 위치
	 * - m_chunkMap은 실제로 로드된 청크가 담겨있는 3D Map
	 */
	m_waitLoadChunkPosMap.clear();
	for (auto& p : m_renderablePosMap) {
		const PosInt3& pos = p.first;

		if (m_chunkMap.find(pos) != m_chunkMap.end())
			continue;

		m_waitLoadChunkPosMap[pos] = true;
	}

	/*
	 * m_unloadChunkList
	 * - 언로드할 청크들의 리스트
	 * - m_chunkMap을 순회하므로 로드된 청크가 들어가게 될 것
	 * - 렌더링가능한 위치가 아니면 unload 리스트에 넣어둠
	 */
	for (auto& p : m_chunkMap) {
		const PosInt3& pos = p.first;
		Chunk* chunk = p.second;

		if (m_renderablePosMap.find(pos) == m_renderablePosMap.end()) {
			m_unloadChunkList.push_back(chunk);
		}
	}
}

void ChunkManager::LoadChunks(Camera& camera)
{
	if (m_initFutures.size() == m_initThreadCount)
		return;

	m_waitLoadChunkPosList.clear();
	for (auto& p : m_waitLoadChunkPosMap) {
		m_waitLoadChunkPosList.push_back(p.first);
	}
	SortPosListByCameraDistance(camera.GetPosition(), m_waitLoadChunkPosList);

	/*
	 * 실질적인 로드 실행 로직
	 * - GetChunkFromPool: 메모리 할당된 청크를 Pool에서 가져와 사용
	 * - GetChunkLoadMemoryFromPool: 청크 로드에 필요한 메모리를 미리 할당 후 Pool에서 가져와 사용
	 * - m_initFutures: <chunk, future> 리스트로 관리하여 동기화할 것
	 */
	while (!m_waitLoadChunkPosList.empty() && m_initFutures.size() < m_initThreadCount) {

		Chunk* chunk = GetChunkFromPool();
		if (chunk == nullptr) {
			return;
		}

		ChunkLoadMemory* chunkLoadMemory = GetChunkLoadMemoryFromPool();
		if (chunkLoadMemory == nullptr) {
			/*
			 * 할당한 Pool 누수 방지
			 * RAII: OOP로 래핑하여 관리할 수 있을 듯 (생성자Get, 소멸자Release)
			 */
			ReleaseChunkToPool(chunk);

			return;
		}

		const PosInt3& pos = m_waitLoadChunkPosList.back();

		/*
		 * std::async(policy, &f, args..)
		 * - 쓰레드 생성 및 실행
		 * - 쓰레드 생성하는게 싫으면 쓰레드 풀로 관리해도 됨
		 *   - 윈도우 VS2022/MSVC 빌드는 윈도우쓰레드풀 위에서 동작한다고 함
		 *   - 쓰레드를 매번 만들지 않는다고 함
		 * 현재는 단순히 std::async를 실행시키고 동기시에 완료가 됐는지 판단함
		 * - 거리에 따른 우선순위 시스템이나, 취소 동작을 할 수 없음
		 * - 거리에 따른 우선순위는 단순히 컨테이너 정렬로 쓰레드 실행 전에만 동작
		 * - 취소 동작은 하지 않고, 로드 완료 시에 필요가 없으면 언로드함
		 */
		m_initFutures.push_back(std::make_pair(chunk,
			std::async(std::launch::async, &Chunk::Initialize, chunk, pos, chunkLoadMemory)));

		m_waitLoadChunkPosList.pop_back();
		m_waitLoadChunkPosMap.erase(pos);
	}
}

void ChunkManager::SyncLoadedChunks()
{
	for (auto it = m_initFutures.begin(); it != m_initFutures.end();) {

		if (it->second.wait_for(std::chrono::microseconds(0)) != std::future_status::ready) {
			++it;
			continue;
		}

		Chunk* chunk = it->first;
		ChunkLoadMemory* chunkLoadMemory = it->second.get();
		it = m_initFutures.erase(it);

		/*
		 * 패치 정보가 chunkLoadMemory에 담겨있음
		 * 이동 시맨틱으로 임시 데이터 공간으로 복사
		 */
		PosHashMap<PatchDataHashSet> loadPatchResult(std::move(chunkLoadMemory->loadPatchResult));
		ReleaseChunkLoadMemoryToPool(chunkLoadMemory);

		/*
		 * 로드된 청크가 렌더링 위치에 존재하는지 검사
		 * 로드된 청크가 렌더링 위치에 있다고 보장할 수 없음
		 * - 멀티쓰레드 환경으로 로드가 늦게되는 경우에 렌더링 거리를 벗어날 수 있음
		 */
		const PosInt3 pos = Utils::VectorToPosInt3(chunk->GetOffsetPosition());
		if (m_renderablePosMap.find(pos) == m_renderablePosMap.end()) {
			ReleaseChunkToPool(chunk);
			continue;
		}

		/*
		 * 로드된 청크가 이미 로드 동기화까지 마친 청크인지 중복 검사
		 * 프레임에 따라 중복된 위치를 Load 했을 경우도 존재하게 됨
		 *  - 로드가 매우 느린 A청크 + 다음 프레임에서 카메라 이동으로 같은 위치에 B청크를 로드 대기
		 *  - A 로드 완료 후 정상 -> B 로드 완료 후 중복
		 */
		if (m_chunkMap.find(pos) != m_chunkMap.end()) {
			ReleaseChunkToPool(chunk);
			continue;
		}

		chunk->SetUpdateRequired(true);
		chunk->SetLoad(true);

		/*
		 * 멀티쓰레드 환경에서 1frame 깜빡임 문제 해결
		 * DrawIndexed(cpuIndices.size()..)
		 * - 로드는 괜찮으나, 로드된 청크가 패치 중에 렌더링하여 undefined 문제가 발생
		 * - 정적인 변수로 사용하여 해결함
		 */
		chunk->UpdateCpuBufferCount();

		/*
		 * ChunkManager가 모든 GPU 버퍼를 관리함
		 * - Instance 렌더링을 위해 GPU 버퍼를 ChunkManager가 모두 관리할 필요가 있었고, 그에 따라
		 * 통일하여
		 * - 일반적인 Vertex 렌더링도 GPU 버퍼를 모두 ChunkManager가 관리함
		 * - 아래에서는 GPU 버퍼를 업데이트 형식으로 사용하는데, 크기가 부족하면 Resize 시켜서
		 * Update하게 됨
		 */
		UpdateChunkGPUBuffer(chunk);

		/*
		 * 패치 정보에 대한 업데이트를 실행함
		 * - 청크가 나무와 같이 주변에 영향을 미치는 경우 주변 청크에 대한 Patch를 진행해야 함
		 * - 다양한 경우에수에 맞춰 의존성-룩업 테이블 관리가 필요해짐에 따라 로직을 구성
		 */
		UpdatePatchChunkMap(chunk, loadPatchResult);

		// 로드가 완료된 청크는 m_chunkMap에 셋업
		m_chunkMap[pos] = chunk;
	}
}

void ChunkManager::UnloadChunks()
{
	/*
	 * 언로드 로직 - 싱글 쓰레드
	 * - 로드된 청크만 들어오기에 들어온 모든 언로드 청크 리스트는 언로드 됨
	 * - 단순히 메모리에서 지우는게 언로드 로직임
	 * GPU 버퍼를 클리어하지 않음
	 * - GPU 버퍼는 풀에서 다시 꺼내서 Update 구성으로 이루어짐
	 * - 그에 따라, 크기를 재할당할 필요가 없어서 Release 하지 않음
	 * CPU 버퍼는 초기화 함
	 * - CPU 버퍼는 청크 내부에서 std::vector로 관리하기 때문에 단순히 vector.clear() 함
	 * - 내부 메모리 사이즈는 그대로 가지고 있을 것
	 * - 청크를 재사용 시 크기 재할당을 피함
	 */
	while (!m_unloadChunkList.empty()) {
		Chunk* chunk = m_unloadChunkList.back();
		m_unloadChunkList.pop_back();

		PosInt3 chunkPos = Utils::VectorToPosInt3(chunk->GetOffsetPosition());

		if (m_chunkMap.find(chunkPos) != m_chunkMap.end())
			m_chunkMap.erase(chunkPos);

		if (m_waitPatchChunkMap.find(chunkPos) != m_waitPatchChunkMap.end())
			m_waitPatchChunkMap.erase(chunkPos);

		if (m_patchedChunkSet.find(chunkPos) != m_patchedChunkSet.end())
			m_patchedChunkSet.erase(chunkPos);

		if (m_patchDependencyMap.find(chunkPos) != m_patchDependencyMap.end()) {

			const auto& patchedChunkMapList = m_patchDependencyMap[chunkPos];

			for (const auto& destChunk : patchedChunkMapList) {
				PosInt3 destChunkPos = destChunk.first;

				if (m_lookupDependencySet.find(destChunkPos) != m_lookupDependencySet.end()) {

					m_lookupDependencySet[destChunkPos].erase(chunkPos);

					if (m_lookupDependencySet[destChunkPos].size() == 0) {
						m_lookupDependencySet.erase(destChunkPos);
					}
				}
			}

			m_patchDependencyMap.erase(chunkPos);
		}

		ReleaseChunkToPool(chunk);
	}
}

void ChunkManager::PatchChunks(Camera& camera)
{
	if (m_patchFutures.size() == m_patchThreadCount)
		return;

	m_waitPatchChunkPosList.clear();
	for (auto& p : m_waitPatchChunkMap) {
		m_waitPatchChunkPosList.push_back(p.first);
	}
	SortPosListByCameraDistance(camera.GetPosition(), m_waitPatchChunkPosList);

	/*
	* 실질적인 청크 패치 실행 로직
	* - 로드된 청크인지, 패치 중이지 않은 청크인지 판단 후 패치 실행
	*/
	for (auto& chunkPos : m_waitPatchChunkPosList) {

		if (m_patchFutures.size() == m_patchThreadCount)
			break;

		if (m_chunkMap.find(chunkPos) == m_chunkMap.end())
			continue;
			
		Chunk* chunk = m_chunkMap[chunkPos];
		if (!chunk->IsLoaded())
			continue;

		if (chunk->IsPatching())
			continue;

		ChunkLoadMemory* chunkLoadMemory = GetChunkLoadMemoryFromPool();
		if (chunkLoadMemory == nullptr)
			break;

		/*
		 * std::async(policy, &f, args..)
		 * - 쓰레드 생성 및 실행
		 * - 쓰레드 생성하는게 싫으면 쓰레드 풀로 관리해도 됨
		 *   - 윈도우 VS2022/MSVC 빌드는 윈도우쓰레드풀 위에서 동작한다고 함
		 *   - 쓰레드를 매번 만들지 않는다고 함
		 * - 패치 데이터는 이동연산자로 불필요한 복사를 줄임
		 */
		m_patchFutures.push_back(
			std::make_pair(chunk, std::async(std::launch::async, &Chunk::Patch, chunk,
									  std::move(m_waitPatchChunkMap[chunkPos]), chunkLoadMemory)));

		m_waitPatchChunkMap.erase(chunkPos);
	}
}

void ChunkManager::SyncPatchedChunks()
{
	for (auto it = m_patchFutures.begin(); it != m_patchFutures.end();) {

		if (it->second.wait_for(std::chrono::microseconds(0)) != std::future_status::ready) {
			++it;
			continue;
		}

		Chunk* chunk = it->first;
		ChunkLoadMemory* chunkLoadMemory = it->second.get();
		it = m_patchFutures.erase(it);

		ReleaseChunkLoadMemoryToPool(chunkLoadMemory);

		const PosInt3 pos = Utils::VectorToPosInt3(chunk->GetOffsetPosition());

		if (m_chunkMap.find(pos) == m_chunkMap.end()) {
			/*
			 * 로드 동기에서는 체크 후 언로드 리스트에 추가했음
			 * - 언로드 리스트 로직에는 로드된 청크만 추가하기 때문
			 * - 안해도 되나, 불필요한 연산을 제거하기 위함, 특히 GPU 버퍼 연산
			 * 패치 동기에서는 체크 후 언로드 리스트에 추가할 필요가 없음
			 * - 이미 언로드된 청크임
			 */
			continue;
		}

		/*
		* 패치 중인 청크도 결국 렌더링을 계속했어야 했음
		* - DrawIndexed(Chunk->GetCPUIndices().size())를 하는 경우: 레이스컨디션 문제가 발생
		* - 청크 내부의 CPU 버퍼가 변경되는 와중에 렌더링을 해버릴 수 있기 때문 (로드가 아닌 패치의 경우에 발생)
		* - 해결: Size에 대한 정보를 따로 담아두고, 로드나 패치가 완료될 때에 데이터를 갱신
		* cf) GPU 버퍼는 ChunkManager가 관리하므로 Chunk의 쓰레드 실행에 대한 레이스컨디션 문제는 생기지 않음
		*/
		chunk->UpdateCpuBufferCount();

		/*
		* Block이 아닌 Instance만 패치되는 경우, 블록에 대한 GPU 버퍼를 Update할 필요가 없음
		* - Instance는 매 프레임 청크를 뒤져가며 따로 GPU 버퍼를 갱신하지만 Blocks는 그렇지 않음
		*/
		if (chunk->OnPatchDirtyFlag()) {
			UpdateChunkGPUBuffer(chunk);

			chunk->SetOnPatchDirtyFlag(false);
		}

		chunk->SetIsPatching(false);
	}
}

void ChunkManager::UpdatePatchChunkMap(
	Chunk* chunk, const PosHashMap<PatchDataHashSet>& loadPatchResult)
{
	/*
	 * 로드가 완료된 청크를 동기화할 때 실행되는 Patch 로직 관련 함수
	 *
	 * m_patchDependencyMap: 내가 영향을 미치는 청크와 패치정보를 담는 의존성 데이터
	 * - m_patchDependencyMap[주체] = { { 객체1: {패치1}, {패치2}, ..}, { 객체2: {패치1}, ..}, ..}
	 *
	 * m_lookupDependencySet: 의존성을 찾는 LUT
	 * - m_lookupDependencySet[객체] = { 주체1, 주체2, 주체3 }
	 * - 패치를 하지 않아도 데이터 담아 차후에 사용: 아래의 m_patchedChunkSet과 성격이 조금 다름
	 *
	 * m_patchedChunkSet: patch 리스트에 등록된 정보를 담음, re Patch 방지
	 * - m_patchedChunkSet[객체] = { 주체1, 주체2, }
	 * - 패치 리스트에 넣은 경우에만 데이터 담아 사용함: 위의 m_lookupDependencySet와 성격이 조금
	 * 다름
	 *
	 * m_waitPatchChunkMap: patch할 청크들을 담음
	 * - m_waitPatchChunkMap[주체] = { 패치1, 패치2, .. }
	 */
	PosInt3 curPos = Utils::VectorToPosInt3(chunk->GetOffsetPosition());

	// 1. 현재 청크: 현재 청크를 패치한 기록이 있으면 패치 정보에 넣을 것
	if (m_lookupDependencySet.find(curPos) != m_lookupDependencySet.end()) {

		for (const auto& srcPos : m_lookupDependencySet[curPos]) {

			if (m_patchDependencyMap.find(srcPos) != m_patchDependencyMap.end() &&
				m_patchDependencyMap[srcPos].find(curPos) != m_patchDependencyMap[srcPos].end()) {

				for (const PatchData& patchData : m_patchDependencyMap[srcPos][curPos]) {
					m_waitPatchChunkMap[curPos].insert(patchData);

					m_patchedChunkSet[curPos].insert(srcPos);
				}
			}
		}
	}

	// 2. 현재 청크: 플레이어에 의해서 수정된 정보를 담음
	if (m_cameraPatchChunkMap.find(curPos) != m_cameraPatchChunkMap.end()) {

		for (const auto& patchData : m_cameraPatchChunkMap[curPos]) {
			m_waitPatchChunkMap[curPos].insert(patchData);
		}
	}

	// 3. 주변 청크: 현재 청크가 미치는 주변에 대한 패치 처리
	for (const auto& [neighborPos, patchDataSet] : loadPatchResult) {
		bool patchFlag = false;

		m_lookupDependencySet[neighborPos].insert(curPos);
		for (const auto& patchData : patchDataSet) {
			m_patchDependencyMap[curPos][neighborPos].insert(patchData);
		}

		if (m_chunkMap.find(neighborPos) == m_chunkMap.end())
			continue;

		if (!m_chunkMap[neighborPos]->IsLoaded())
			continue;

		if (m_patchedChunkSet[neighborPos].find(curPos) != m_patchedChunkSet[neighborPos].end())
			continue;

		for (const auto& patchData : patchDataSet) {
			m_waitPatchChunkMap[neighborPos].insert(patchData);
		}

		m_patchedChunkSet[neighborPos].insert(curPos);
	}
}

void ChunkManager::UpdateRenderChunkList(Camera& camera, const Light& light)
{
	m_renderChunkList.clear();
	m_renderMirrorChunkList.clear();
	m_renderShadowChunkList.clear();

	for (auto& p : m_chunkMap) {
		Chunk* chunk = p.second;

		if (!chunk->IsLoaded())
			continue;

		if (chunk->IsEmpty()) {
			continue;
		}

		Vector3 chunkPos = chunk->GetPosition();

		if (FrustumCulling(chunkPos, camera, light, false, false)) {
			m_renderChunkList.push_back(chunk);
		}

		for (int i = 0; i < Light::CASCADE_LEVEL; ++i) {
			if (FrustumCulling(chunkPos, camera, light, false, true, i)) {
				m_renderShadowChunkList.push_back(chunk);
				break;
			}
		}

		Vector3 mirrorChunkPos = Vector3::Transform(chunkPos, camera.GetMirrorPlaneMatrix());
		if (FrustumCulling(mirrorChunkPos, camera, light, true, false)) {
			m_renderMirrorChunkList.push_back(chunk);
		}
	}
}

void ChunkManager::UpdateInstanceInfoList(Camera& camera)
{
	// clear all info
	for (int i = 0; i < INSTANCE_SHAPE::INSTANCE_SHAPE_COUNT; ++i)
		m_instanceInfoList[i].clear();

	// check instance in chunk managerList
	for (auto& c : m_renderChunkList) {
		// check stable
		if (c->IsUpdateRequired())
			continue;

		// check distance
		Vector3 chunkPosition = c->GetPosition();
		Vector3 chunkCenterPosition = chunkPosition + Vector3(Chunk::CHUNK_SIZE * 0.5);
		Vector3 diffPosition = chunkCenterPosition - camera.GetPosition();
		if (diffPosition.Length() > (float)Camera::LOD_RENDER_DISTANCE)
			continue;

		// set info
		const PosHashMap<Instance>& instanceMap = c->GetInstanceMap();
		for (auto& p : instanceMap) {
			const PosInt3& localPos = p.first;
			const Instance& instance = p.second;

			Vector3 worldPosition = c->GetOffsetPosition() + Utils::PosInt3ToVector(localPos);

			if (instance.GetFaceFlag() > 0) {
				AddInstanceInfoBySplitFace(worldPosition, instance);
			}
			else {
				AddInstanceInfo(worldPosition, instance);
			}
		}
	}

	for (int i = 0; i < INSTANCE_SHAPE::INSTANCE_SHAPE_COUNT; ++i) {
		DXUtils::ResizeBuffer(m_instanceInfoBuffers[i], m_instanceInfoList[i],
			(UINT)D3D11_BIND_VERTEX_BUFFER, m_instanceInfoList[i].size() + 1024);
		DXUtils::UpdateBuffer(m_instanceInfoBuffers[i], m_instanceInfoList[i]);
	}
}

void ChunkManager::UpdateChunkConstant(float dt)
{
	for (auto& p : m_chunkMap) {
		Chunk* chunk = p.second;

		if (!chunk->IsLoaded())
			continue;

		if (!chunk->IsUpdateRequired())
			continue;

		chunk->Update(dt);

		ChunkConstantData tempConstantData;
		tempConstantData.world = chunk->GetConstantData().world.Transpose();

		if (m_constantBuffers[chunk->GetID()]) {
			DXUtils::UpdateConstantBuffer(m_constantBuffers[chunk->GetID()], tempConstantData);
		}
	}
}

void ChunkManager::AddInstanceInfo(Vector3 worldPosition, const Instance& instance)
{
	InstanceInfoVertex info;

	INSTANCE_TYPE type = instance.GetType();
	info.texIndex = instance.GetTexIndex();

	float offsetNoiseX = instance.GetOffsetNoisePositionXZ().x;
	float offsetNoiseZ = instance.GetOffsetNoisePositionXZ().y;
	Vector3 offsetNoiseXZ = Vector3(0.5f) + Vector3(offsetNoiseX, 0.0f, offsetNoiseZ);
	Vector3 instanceWorldPosition = worldPosition + offsetNoiseXZ;
	Matrix translation = Matrix::CreateTranslation(instanceWorldPosition);

	float yawRotationRadian = instance.GetYawRotation() * (XM_PI / 180.0f);
	Matrix rotation = Matrix::CreateFromQuaternion(
		Quaternion::CreateFromAxisAngle(Vector3(0.0f, 1.0f, 0.0f), yawRotationRadian));

	info.instanceWorld = (rotation * translation).Transpose();

	INSTANCE_SHAPE shapeType = instance.GetShape(type);
	m_instanceInfoList[shapeType].push_back(info);
}

void ChunkManager::AddInstanceInfoBySplitFace(Vector3 worldPosition, const Instance& instance)
{
	uint8_t faceFlag = instance.GetFaceFlag();

	if (faceFlag & (1 << VINE_DIR::V_LEFT)) {
		Instance splitedInstance = instance;
		splitedInstance.SetYawRotation(270.0f);
		AddInstanceInfo(worldPosition, splitedInstance);
	}

	if (faceFlag & (1 << VINE_DIR::V_RIGHT)) {
		Instance splitedInstance = instance;
		splitedInstance.SetYawRotation(90.0f);
		AddInstanceInfo(worldPosition, splitedInstance);
	}

	if (faceFlag & (1 << VINE_DIR::V_FRONT)) {
		Instance splitedInstance = instance;
		splitedInstance.SetYawRotation(180.0f);
		AddInstanceInfo(worldPosition, splitedInstance);
	}

	if (faceFlag & (1 << VINE_DIR::V_BACK)) {
		Instance splitedInstance = instance;
		splitedInstance.SetYawRotation(0.0f);
		AddInstanceInfo(worldPosition, splitedInstance);
	}
}

bool ChunkManager::FrustumCulling(Vector3 position, const Camera& camera, const Light& light,
	bool useMirror, bool useShadow, int index)
{
	Matrix invMat = Matrix();

	if (useShadow) {
		invMat = (light.GetViewMatrix() * light.GetProjectionMatrixFromCascade(index)).Invert();
	}
	else {
		invMat = (camera.GetViewMatrix() * camera.GetProjectionMatrix()).Invert();
	}

	// Transformed view frustum NDC Position to world position
	std::vector<Vector3> worldPos = { Vector3::Transform(Vector3(-1.0f, 1.0f, 0.0f), invMat),
		Vector3::Transform(Vector3(1.0f, 1.0f, 0.0f), invMat),
		Vector3::Transform(Vector3(1.0f, -1.0f, 0.0f), invMat),
		Vector3::Transform(Vector3(-1.0f, -1.0f, 0.0f), invMat),
		Vector3::Transform(Vector3(-1.0f, 1.0f, 1.0f), invMat),
		Vector3::Transform(Vector3(1.0f, 1.0f, 1.0f), invMat),
		Vector3::Transform(Vector3(1.0f, -1.0f, 1.0f), invMat),
		Vector3::Transform(Vector3(-1.0f, -1.0f, 1.0f), invMat) };

	std::vector<Vector4> vfPlanes = {
		DirectX::XMPlaneFromPoints(worldPos[0], worldPos[1], worldPos[2]), // front
		DirectX::XMPlaneFromPoints(worldPos[7], worldPos[6], worldPos[5]), // back
		DirectX::XMPlaneFromPoints(worldPos[4], worldPos[5], worldPos[1]), // top
		DirectX::XMPlaneFromPoints(worldPos[3], worldPos[2], worldPos[6]), // bottom
		DirectX::XMPlaneFromPoints(worldPos[4], worldPos[0], worldPos[3]), // left
		DirectX::XMPlaneFromPoints(worldPos[1], worldPos[5], worldPos[6])  // right
	};

	float x = (float)Chunk::CHUNK_SIZE;
	float y = (float)Chunk::CHUNK_SIZE;
	float z = (float)Chunk::CHUNK_SIZE;
	if (useMirror)
		y *= -1;

	for (int i = 0; i < vfPlanes.size(); ++i) {
		if (XMVectorGetX(XMPlaneDotCoord(vfPlanes[i], position)) <= 0.0f)
			continue;
		if (XMVectorGetX(XMPlaneDotCoord(vfPlanes[i], position + Vector3(x, 0.0f, 0.0f))) <= 0.0f)
			continue;
		if (XMVectorGetX(XMPlaneDotCoord(vfPlanes[i], position + Vector3(0.0f, y, 0.0f))) <= 0.0f)
			continue;
		if (XMVectorGetX(XMPlaneDotCoord(vfPlanes[i], position + Vector3(x, y, 0.0f))) <= 0.0f)
			continue;
		if (XMVectorGetX(XMPlaneDotCoord(vfPlanes[i], position + Vector3(0.0f, 0.0f, z))) <= 0.0f)
			continue;
		if (XMVectorGetX(XMPlaneDotCoord(vfPlanes[i], position + Vector3(x, 0.0f, z))) <= 0.0f)
			continue;
		if (XMVectorGetX(XMPlaneDotCoord(vfPlanes[i], position + Vector3(0.0f, y, z))) <= 0.0f)
			continue;
		if (XMVectorGetX(XMPlaneDotCoord(vfPlanes[i], position + Vector3(x, y, z))) <= 0.0f)
			continue;
		return false;
	}

	return true;
}

void ChunkManager::UpdateChunkGPUBuffer(Chunk* chunk)
{
	if (chunk->IsEmpty())
		return;

	UINT id = chunk->GetID();

	// constant data
	ChunkConstantData tempConstantData = chunk->GetConstantData();
	tempConstantData.world = tempConstantData.world.Transpose();

	if (!m_constantBuffers[id])
		DXUtils::CreateConstantBuffer(m_constantBuffers[id], tempConstantData);
	else
		DXUtils::UpdateConstantBuffer(m_constantBuffers[id], tempConstantData);

	// lowLod
	if (!chunk->IsEmptyLowLod()) {
		DXUtils::ResizeBuffer(
			m_lowLodVertexBuffers[id], chunk->GetLowLodVertices(), (UINT)D3D11_BIND_VERTEX_BUFFER);
		DXUtils::ResizeBuffer(
			m_lowLodIndexBuffers[id], chunk->GetLowLodIndices(), (UINT)D3D11_BIND_INDEX_BUFFER);

		DXUtils::UpdateBuffer(m_lowLodVertexBuffers[id], chunk->GetLowLodVertices());
		DXUtils::UpdateBuffer(m_lowLodIndexBuffers[id], chunk->GetLowLodIndices());
	}

	// opaque
	if (!chunk->IsEmptyOpaque()) {
		DXUtils::ResizeBuffer(
			m_opaqueVertexBuffers[id], chunk->GetOpaqueVertices(), (UINT)D3D11_BIND_VERTEX_BUFFER);
		DXUtils::ResizeBuffer(
			m_opaqueIndexBuffers[id], chunk->GetOpaqueIndices(), (UINT)D3D11_BIND_INDEX_BUFFER);

		DXUtils::UpdateBuffer(m_opaqueVertexBuffers[id], chunk->GetOpaqueVertices());
		DXUtils::UpdateBuffer(m_opaqueIndexBuffers[id], chunk->GetOpaqueIndices());
	}

	// transparency
	if (!chunk->IsEmptyTransparency()) {
		DXUtils::ResizeBuffer(m_transparencyVertexBuffers[id], chunk->GetTransparencyVertices(),
			(UINT)D3D11_BIND_VERTEX_BUFFER);
		DXUtils::ResizeBuffer(m_transparencyIndexBuffers[id], chunk->GetTransparencyIndices(),
			(UINT)D3D11_BIND_INDEX_BUFFER);

		DXUtils::UpdateBuffer(m_transparencyVertexBuffers[id], chunk->GetTransparencyVertices());
		DXUtils::UpdateBuffer(m_transparencyIndexBuffers[id], chunk->GetTransparencyIndices());
	}

	// semiAlpha
	if (!chunk->IsEmptySemiAlpha()) {
		DXUtils::ResizeBuffer(m_semiAlphaVertexBuffers[id], chunk->GetSemiAlphaVertices(),
			(UINT)D3D11_BIND_VERTEX_BUFFER);
		DXUtils::ResizeBuffer(m_semiAlphaIndexBuffers[id], chunk->GetSemiAlphaIndices(),
			(UINT)D3D11_BIND_INDEX_BUFFER);

		DXUtils::UpdateBuffer(m_semiAlphaVertexBuffers[id], chunk->GetSemiAlphaVertices());
		DXUtils::UpdateBuffer(m_semiAlphaIndexBuffers[id], chunk->GetSemiAlphaIndices());
	}
}

void ChunkManager::InitWorkerThreadCount()
{
	int maxThreadCount = std::thread::hardware_concurrency();
	if (maxThreadCount == 0)
		maxThreadCount = 4;

	const int reservedThreadCount = 2;
	int workerThreadCount = max(2, maxThreadCount - reservedThreadCount);

	m_patchThreadCount = max(1u, workerThreadCount / 2);
	m_initThreadCount = max(1u, workerThreadCount - m_patchThreadCount);
}

void ChunkManager::InitChunkPool()
{
	m_chunkPool.reserve(CHUNK_POOL_SIZE);

	for (int id = 0; id < CHUNK_POOL_SIZE; ++id) {
		Chunk* chunk = new Chunk(id);
		chunk->Clear();

		m_chunkPool.push_back(chunk);
	}
}

Chunk* ChunkManager::GetChunkFromPool()
{
	if (!m_chunkPool.empty()) {
		Chunk* chunk = m_chunkPool.back();
		m_chunkPool.pop_back();

		return chunk;
	}

	return nullptr;
}

void ChunkManager::ReleaseChunkToPool(Chunk* chunk)
{
	chunk->Clear();

	m_chunkPool.push_back(chunk);
}

void ChunkManager::InitChunkLoadMemoryPool()
{
	for (unsigned int i = 0; i < m_initThreadCount + m_patchThreadCount; ++i) {
		m_chunkLoadMemoryPool.push_back(new ChunkLoadMemory());
	}
}

ChunkLoadMemory* ChunkManager::GetChunkLoadMemoryFromPool()
{
	if (!m_chunkLoadMemoryPool.empty()) {
		ChunkLoadMemory* chunkLoadMemory = m_chunkLoadMemoryPool.back();
		m_chunkLoadMemoryPool.pop_back();

		return chunkLoadMemory;
	}

	return nullptr;
}

void ChunkManager::ReleaseChunkLoadMemoryToPool(ChunkLoadMemory* chunkLoadMemory)
{
	chunkLoadMemory->Clear();

	m_chunkLoadMemoryPool.push_back(chunkLoadMemory);
}

bool ChunkManager::MakeInstanceVertexBuffer()
{
	std::vector<InstanceVertex> instanceVertices;
	std::vector<uint32_t> instanceIndices;

	// Instance Type 0 : CROSS
	MeshGenerator::CreateCrossInstanceMesh(instanceVertices, instanceIndices);
	if (!DXUtils::CreateVertexBuffer(
			m_instanceVertexBuffers[INSTANCE_SHAPE::INSTANCE_CROSS], instanceVertices)) {
		std::cout << "failed create cross instance vertex buffer in chunk manager" << std::endl;
		return false;
	}
	if (!DXUtils::CreateIndexBuffer(
			m_instanceIndexBuffers[INSTANCE_SHAPE::INSTANCE_CROSS], instanceIndices)) {
		std::cout << "failed create cross instance index buffer in chunk manager" << std::endl;
		return false;
	}
	instanceVertices.clear();
	instanceIndices.clear();


	// Instance Type 1 : FENCE
	MeshGenerator::CreateFenceInstanceMesh(instanceVertices, instanceIndices);
	if (!DXUtils::CreateVertexBuffer(
			m_instanceVertexBuffers[INSTANCE_SHAPE::INSTANCE_FENCE], instanceVertices)) {
		std::cout << "failed create fence instance vertex buffer in chunk manager" << std::endl;
		return false;
	}
	if (!DXUtils::CreateIndexBuffer(
			m_instanceIndexBuffers[INSTANCE_SHAPE::INSTANCE_FENCE], instanceIndices)) {
		std::cout << "failed create fence instance index buffer in chunk manager" << std::endl;
		return false;
	}
	instanceVertices.clear();
	instanceIndices.clear();


	// Instance Type 2 : SQUARE
	MeshGenerator::CreateSquareInstanceMesh(instanceVertices, instanceIndices);
	if (!DXUtils::CreateVertexBuffer(
			m_instanceVertexBuffers[INSTANCE_SHAPE::INSTANCE_SQUARE], instanceVertices)) {
		std::cout << "failed create SQUARE instance vertex buffer in chunk manager" << std::endl;
		return false;
	}
	if (!DXUtils::CreateIndexBuffer(
			m_instanceIndexBuffers[INSTANCE_SHAPE::INSTANCE_SQUARE], instanceIndices)) {
		std::cout << "failed create SQUARE instance index buffer in chunk manager" << std::endl;
		return false;
	}
	instanceVertices.clear();
	instanceIndices.clear();


	// Instance Type 3 : FLOOR
	MeshGenerator::CreateFloorInstanceMesh(instanceVertices, instanceIndices);
	if (!DXUtils::CreateVertexBuffer(
			m_instanceVertexBuffers[INSTANCE_SHAPE::INSTANCE_FLOOR], instanceVertices)) {
		std::cout << "failed create FLOOR instance vertex buffer in chunk manager" << std::endl;
		return false;
	}
	if (!DXUtils::CreateIndexBuffer(
			m_instanceIndexBuffers[INSTANCE_SHAPE::INSTANCE_FLOOR], instanceIndices)) {
		std::cout << "failed create FLOOR instance index buffer in chunk manager" << std::endl;
		return false;
	}

	return true;
}

bool ChunkManager::MakeInstanceInfoBuffer()
{
	for (auto& instanceBuffer : m_instanceInfoBuffers) {
		if (!DXUtils::CreateDynamicBuffer(instanceBuffer, MAX_INSTANCE_BUFFER_COUNT,
				sizeof(InstanceInfoVertex),
				(UINT)D3D11_BIND_VERTEX_BUFFER)) { // 약 8MB
			std::cout << "failed create instance info buffer in chunk manager" << std::endl;
			return false;
		}
	}

	return true;
}

const Chunk* ChunkManager::GetChunkByPosition(Vector3 position)
{
	Vector3 chunkPos = Utils::CalcOffsetPos(position, Chunk::CHUNK_SIZE);

	auto iter = m_chunkMap.find(Utils::VectorToPosInt3(chunkPos));

	if (iter == m_chunkMap.end())
		return nullptr;

	return iter->second;
}

const Block* ChunkManager::GetBlockByPosition(Vector3 position)
{
	const Chunk* c = GetChunkByPosition(position);

	if (c != nullptr && c->IsLoaded()) {
		Vector3 blockLocalPosition = position - Utils::CalcOffsetPos(position, Chunk::CHUNK_SIZE);
		return c->GetBlock(blockLocalPosition);
	}

	return nullptr;
}

const Instance* ChunkManager::GetInstanceByPosition(Vector3 position)
{
	const Chunk* c = GetChunkByPosition(position);

	if (c != nullptr && c->IsLoaded()) {
		Vector3 InstanceLocalPosition =
			position - Utils::CalcOffsetPos(position, Chunk::CHUNK_SIZE);
		return c->GetInstance(InstanceLocalPosition);
	}

	return nullptr;
}

bool ChunkManager::HasObjectAt(Vector3 pickingBlockPos)
{
	const Instance* pickingInstance = GetInstanceByPosition(pickingBlockPos);
	if (pickingInstance) {
		return true;
	}

	const Block* pickingBlock = GetBlockByPosition(pickingBlockPos);
	if (pickingBlock && !Block::IsTransparency(pickingBlock->GetType())) {
		return true;
	}

	return false;
}

void ChunkManager::RemoveBlockPatchAt(Vector3 pickingBlockPos)
{
	Vector3 chunkOffsetPos = Utils::CalcOffsetPos(pickingBlockPos, Chunk::CHUNK_SIZE);
	PosInt3 chunkOffsetPosInt3 = Utils::VectorToPosInt3(chunkOffsetPos);

	Vector3 blockLocalPos = pickingBlockPos - chunkOffsetPos;

	BLOCK_TYPE blockType = pickingBlockPos.y <= Terrain::WATER_HEIGHT_LEVEL
							   ? BLOCK_TYPE::BLOCK_WATER
							   : BLOCK_TYPE::BLOCK_AIR;

	PatchData patchData(blockLocalPos, Block(blockType), Instance(), Chunk::CHUNK_SIZE, false);

	m_cameraPatchChunkMap[chunkOffsetPosInt3].insert(patchData);
	if (m_chunkMap.find(chunkOffsetPosInt3) != m_chunkMap.end() &&
		m_chunkMap[chunkOffsetPosInt3]->IsLoaded()) {
		m_waitPatchChunkMap[chunkOffsetPosInt3].insert(patchData);
	}

	PropagatePatchByEdgeBlock(blockLocalPos, chunkOffsetPos, blockType);
}

void ChunkManager::AddBlockPatchAt(Vector3 position, DIR face)
{
	Vector3 faceOffset = Vector3(0.0f);
	if (face == DIR::LEFT) {
		faceOffset.x--;
	}
	else if (face == DIR::RIGHT) {
		faceOffset.x++;
	}
	else if (face == DIR::BOTTOM) {
		faceOffset.y--;
	}
	else if (face == DIR::TOP) {
		faceOffset.y++;
	}
	else if (face == DIR::FRONT) {
		faceOffset.z--;
	}
	else if (face == DIR::BACK) {
		faceOffset.z++;
	}

	Vector3 facePosition = position + faceOffset;
	Vector3 chunkOffsetPos = Utils::CalcOffsetPos(facePosition, Chunk::CHUNK_SIZE);
	PosInt3 chunkOffsetPosInt3 = Utils::VectorToPosInt3(chunkOffsetPos);

	Vector3 blockLocalPos = facePosition - chunkOffsetPos;

	BLOCK_TYPE blockType = BLOCK_TYPE::BLOCK_GOLD;

	PatchData patchData(blockLocalPos, Block(blockType), Instance(), Chunk::CHUNK_SIZE, false);

	m_cameraPatchChunkMap[chunkOffsetPosInt3].insert(patchData);
	if (m_chunkMap.find(chunkOffsetPosInt3) != m_chunkMap.end() &&
		m_chunkMap[chunkOffsetPosInt3]->IsLoaded()) {

		m_waitPatchChunkMap[chunkOffsetPosInt3].insert(patchData);
	}

	PropagatePatchByEdgeBlock(blockLocalPos, chunkOffsetPos, blockType);
}

void ChunkManager::PropagatePatchByEdgeBlock(
	Vector3 localPosition, Vector3 chunkOffsetPos, BLOCK_TYPE blockType)
{
	std::pair<PosInt3, PatchData> outEdgePatchEntry[3];
	int outEdgePatchEntryCount = 0;
	PatchData::GenerateEdgePatchEntry(localPosition, chunkOffsetPos, blockType, Chunk::CHUNK_SIZE,
		outEdgePatchEntry, outEdgePatchEntryCount);

	for (int i = 0; i < outEdgePatchEntryCount; ++i) {
		PosInt3& patchChunkPosInt3 = outEdgePatchEntry[i].first;
		PatchData& patchData = outEdgePatchEntry[i].second;

		m_cameraPatchChunkMap[patchChunkPosInt3].insert(patchData);
		if (m_chunkMap.find(patchChunkPosInt3) != m_chunkMap.end() &&
			m_chunkMap[patchChunkPosInt3]->IsLoaded()) {
			m_waitPatchChunkMap[patchChunkPosInt3].insert(patchData);
		}
	}
}

void ChunkManager::SortPosListByCameraDistance(Vector3 cameraPos, std::vector<PosInt3>& posList)
{
	std::sort(posList.begin(), posList.end(), [&cameraPos](PosInt3 a, PosInt3 b) {
		Vector3 aPos = Utils::PosInt3ToVector(a);
		Vector3 bPos = Utils::PosInt3ToVector(b);

		Vector3 aDiff = aPos - cameraPos;
		Vector3 bDiff = bPos - cameraPos;

		float aDiffLengthXZ = Vector2(aDiff.x, aDiff.z).Length();
		float bDiffLengthXZ = Vector2(bDiff.x, bDiff.z).Length();

		if (aDiffLengthXZ == bDiffLengthXZ) {
			return aPos.y < bPos.y;
		}

		return aDiffLengthXZ > bDiffLengthXZ;
	});
}