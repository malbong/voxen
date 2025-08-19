#include "ChunkManager.h"
#include "Graphics.h"
#include "Utils.h"
#include "DXUtils.h"
#include "MeshGenerator.h"
#include "Block.h"
#include "Instance.h"

#include <iostream>
#include <algorithm>

ChunkManager* ChunkManager::chunkManager = nullptr;

ChunkManager* ChunkManager::GetInstance()
{
	if (chunkManager == nullptr) {
		chunkManager = new ChunkManager();
	}

	return chunkManager;
}

ChunkManager::ChunkManager() {}

ChunkManager::~ChunkManager() {}

ChunkManager::ChunkManager(const ChunkManager& other) {}

void ChunkManager::operator=(const ChunkManager& rhs) {}

bool ChunkManager::Initialize(Vector3 cameraChunkPos)
{
	uint32_t maxThreads = min(6u, std::thread::hardware_concurrency());
	uint32_t usableThreads = (maxThreads > 1) ? maxThreads - 1 : 1;
	m_initThreadCount = std::clamp(usableThreads - 1u, 1u, 3u);
	m_patchThreadCount = std::clamp(usableThreads - m_initThreadCount, 1u, 2u);

	for (unsigned int i = 0; i < m_initThreadCount + m_patchThreadCount; ++i) {
		m_chunkLoadMemoryPool.push_back(new ChunkLoadMemory());
	}

	for (int i = 0; i < CHUNK_POOL_SIZE; ++i) {
		Chunk* chunk = new Chunk(i);
		chunk->Clear();
		m_chunkPool.push_back(chunk);
	}

	m_lowLodVertexBuffers.resize(CHUNK_POOL_SIZE, nullptr);
	m_lowLodIndexBuffers.resize(CHUNK_POOL_SIZE, nullptr);

	m_opaqueVertexBuffers.resize(CHUNK_POOL_SIZE, nullptr);
	m_opaqueIndexBuffers.resize(CHUNK_POOL_SIZE, nullptr);

	m_transparencyVertexBuffers.resize(CHUNK_POOL_SIZE, nullptr);
	m_transparencyIndexBuffers.resize(CHUNK_POOL_SIZE, nullptr);

	m_semiAlphaVertexBuffers.resize(CHUNK_POOL_SIZE, nullptr);
	m_semiAlphaIndexBuffers.resize(CHUNK_POOL_SIZE, nullptr);

	m_constantBuffers.resize(CHUNK_POOL_SIZE, nullptr);

	m_instanceVertexBuffers.resize(Instance::INSTANCE_TYPE_COUNT, nullptr);
	m_instanceIndexBuffers.resize(Instance::INSTANCE_TYPE_COUNT, nullptr);
	m_instanceInfoBuffers.resize(Instance::INSTANCE_TYPE_COUNT, nullptr);
	m_instanceInfoList.resize(Instance::INSTANCE_TYPE_COUNT);
	if (!MakeInstanceVertexBuffer())
		return false;
	if (!MakeInstanceInfoBuffer())
		return false;

	UpdateChunkList(cameraChunkPos);

	return true;
}

void ChunkManager::Update(float dt, Camera& camera, Light& light, bool mouseLeftDown, bool mouseRightDown)
{
	if (camera.m_isOnChunkDirtyFlag) {
		UpdateChunkList(camera.GetChunkPosition());
		camera.m_isOnChunkDirtyFlag = false;
	}

	if (camera.HasPickingObject()) {
		if (mouseLeftDown) {
			RemoveBlockPatchAt(camera.GetPickingObjectPosition());
		}
		if (mouseRightDown) {
			AddBlockPatchAt(camera.GetPickingObjectPosition(), camera.GetPickingObjectFace());
		}
	}

	UpdateLoadChunkList(camera);
	UpdateUnloadChunkList();
	UpdatePatchChunkMap(camera);
	UpdateRenderChunkList(camera, light);
	UpdateInstanceInfoList(camera);
	UpdateChunkConstant(dt);
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
	UINT indexCountPerInstance[3] = { 12, 24, 6 };

	for (int i = 0; i < Instance::INSTANCE_TYPE_COUNT; ++i) {
		Graphics::context->IASetIndexBuffer(
			m_instanceIndexBuffers[i].Get(), DXGI_FORMAT_R32_UINT, 0);

		std::vector<UINT> strides = { sizeof(InstanceVertex), sizeof(InstanceInfoVertex) };
		std::vector<UINT> offsets = { 0, 0 };
		std::vector<ID3D11Buffer*> buffers = { m_instanceVertexBuffers[i].Get(),
			m_instanceInfoBuffers[i].Get() };
		Graphics::context->IASetVertexBuffers(0, 2, buffers.data(), strides.data(), offsets.data());
		Graphics::context->DrawIndexedInstanced(
			indexCountPerInstance[i], (UINT)m_instanceInfoList[i].size(), 0, 0, 0);
	}
}

void ChunkManager::RenderBasic(Vector3 cameraPos)
{
	for (auto& c : m_renderChunkList) {
		Vector3 chunkOffset = c->GetOffsetPosition();
		Vector3 chunkCenterPosition = chunkOffset + Vector3(Chunk::CHUNK_SIZE * 0.5);
		Vector3 diffPosition = chunkCenterPosition - cameraPos;

		Graphics::SetPipelineStates(Graphics::basicPSO);
		if (diffPosition.Length() > (float)Camera::LOD_RENDER_DISTANCE) {
			RenderLowLodChunk(c);
		}
		else {
			RenderOpaqueChunk(c);

			Graphics::SetPipelineStates(Graphics::semiAlphaPSO);
			RenderSemiAlphaChunk(c);
		}
	}

	Graphics::SetPipelineStates(Graphics::instancePSO);
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

void ChunkManager::UpdateChunkList(Vector3 cameraChunkPos)
{
	std::map<std::tuple<int, int, int>, bool> renderableChunkMap;
	for (int i = 0; i < MAX_HEIGHT_CHUNK_COUNT; ++i) {
		for (int j = 0; j < CHUNK_COUNT; ++j) {
			for (int k = 0; k < CHUNK_COUNT; ++k) {
				int y = Chunk::CHUNK_SIZE * i;
				int x = (int)cameraChunkPos.x + Chunk::CHUNK_SIZE * (j - CHUNK_COUNT / 2);
				int z = (int)cameraChunkPos.z + Chunk::CHUNK_SIZE * (k - CHUNK_COUNT / 2);

				if (m_chunkMap.find(std::make_tuple(x, y, z)) ==
					m_chunkMap.end()) { // found chunk to be loaded
					Chunk* chunk = GetChunkFromPool();
					if (chunk) {
						chunk->SetOffsetPosition(Vector3((float)x, (float)y, (float)z));

						m_chunkMap[std::make_tuple(x, y, z)] = chunk;
						m_loadChunkList.push_back(chunk);
					}
				}
				else
					renderableChunkMap[std::make_tuple(x, y, z)] = true;
			}
		}
	}

	for (auto& p : m_chunkMap) { // { 1, 2, 3 } -> { 1, 2 } : 3 unload
		if (renderableChunkMap.find(p.first) == renderableChunkMap.end() &&
			m_chunkMap[p.first]->IsLoaded()) {
			m_unloadChunkList.push_back(p.second);
		}
	}
}

void ChunkManager::UpdateLoadChunkList(Camera& camera)
{
	std::sort(m_loadChunkList.begin(), m_loadChunkList.end(), [&camera](Chunk* a, Chunk* b) {
		Vector3 aDiff = (a->GetOffsetPosition() - camera.GetPosition());
		Vector3 bDiff = (b->GetOffsetPosition() - camera.GetPosition());

		float aDiffLengthXZ = Vector2(aDiff.x, aDiff.z).Length();
		float bDiffLengthXZ = Vector2(bDiff.x, bDiff.z).Length();

		if (aDiffLengthXZ == bDiffLengthXZ) {
			return a->GetOffsetPosition().y < b->GetOffsetPosition().y;
		}
		return aDiffLengthXZ > bDiffLengthXZ;
	});
	
	while (!m_loadChunkList.empty() && m_initFutures.size() < m_initThreadCount) {
		Chunk* chunk = m_loadChunkList.back();
		m_loadChunkList.pop_back();

		ChunkLoadMemory* chunkLoadMemory = m_chunkLoadMemoryPool.back();
		m_chunkLoadMemoryPool.pop_back();

		m_initFutures.push_back(std::make_pair(
			chunk, std::async(std::launch::async, &Chunk::Initialize, chunk, chunkLoadMemory)));
	}

	for (auto it = m_initFutures.begin(); it != m_initFutures.end();) {
		if (it->second.wait_for(std::chrono::microseconds(0)) == std::future_status::ready) {
			Chunk* chunk = it->first;
			ChunkLoadMemory* chunkLoadMemory = it->second.get();

			// Dependency Map 구성
			std::tuple<int, int, int> current = Utils::VectorToIntTuple(chunk->GetOffsetPosition());
			for (const auto& [targetPos, patchDataList] : chunkLoadMemory->chunkPatchDataMap) {
				std::tuple<int, int, int> target = Utils::VectorToIntTuple(targetPos);

				bool patchFlag = false;
				for (const auto& patchData : patchDataList) {
					if (m_chunkMap.find(target) == m_chunkMap.end())
						continue;

					if (!m_chunkMap[target]->IsLoaded())
						continue;
	
					// current로 인해 target을 패치한 정보가 없다면 패치할 것
					if (m_patchedChunkSet.find(target) == m_patchedChunkSet.end() ||
						m_patchedChunkSet[target].find(current) == m_patchedChunkSet[target].end()) {

						m_patchChunkMap[target].push_back(patchData);

						patchFlag = true;
					}

					m_dependencyMapList[current][target].push_back(patchData);
				}

				m_lookupDependencySet[target].insert(current);

				// patch를 진행한 target인 경우, patched set에 기록해두고, 수정한 정보에 대한 처리
				if (patchFlag) {
					m_patchedChunkSet[target].insert(current);

					if (m_cameraPatchDataListMap.find(target) != m_cameraPatchDataListMap.end()) {
						for (const auto& patchData : m_cameraPatchDataListMap[target]) {
							m_patchChunkMap[target].push_back(patchData);
						}
					}
				}
			}

			// 월드: 본인 청크에 대한 패치정보가 담긴 Dependency Map 확인 후 있으면 List에 넣음
			if (m_lookupDependencySet.find(current) != m_lookupDependencySet.end()) {
				for (const auto& source : m_lookupDependencySet[current]) {
					if (m_dependencyMapList.find(source) != m_dependencyMapList.end() && 
						m_dependencyMapList[source].find(current) != m_dependencyMapList[source].end()) {
						for (const auto& patchData : m_dependencyMapList[source][current]) {
							m_patchChunkMap[current].push_back(patchData);
							m_patchedChunkSet[current].insert(source);
						}
					}
				}
			}

			// 액션: 플레이어에 의해서 수정된 정보를 담음
			if (m_cameraPatchDataListMap.find(current) != m_cameraPatchDataListMap.end()) {
				for (const auto& patchData : m_cameraPatchDataListMap[current]) {
					m_patchChunkMap[current].push_back(patchData);
				}
			}
			
			// update vertex and index count value for multi threading
			chunk->UpdateCpuBufferCount();

			UpdateChunkBuffer(chunk);

			chunk->SetUpdateRequired(true);
			chunk->SetLoad(true);

			chunkLoadMemory->Clear();
			m_chunkLoadMemoryPool.push_back(chunkLoadMemory);

			it = m_initFutures.erase(it);
		}
		else {
			++it;
		}
	}
}

void ChunkManager::UpdateUnloadChunkList()
{
	while (!m_unloadChunkList.empty()) {
		Chunk* chunk = m_unloadChunkList.back();
		m_unloadChunkList.pop_back();

		std::tuple<int, int, int> chunkPos = Utils::VectorToIntTuple(chunk->GetOffsetPosition());

		if (m_chunkMap.find(chunkPos) != m_chunkMap.end())
			m_chunkMap.erase(chunkPos);

		if (m_patchChunkMap.find(chunkPos) != m_patchChunkMap.end())
			m_patchChunkMap.erase(chunkPos);

		if (m_dependencyMapList.find(chunkPos) != m_dependencyMapList.end()) {
			const auto& patchedChunkMapList = m_dependencyMapList[chunkPos];
			for (const auto& destChunk : patchedChunkMapList) {
				std::tuple<int, int, int> destChunkPos = destChunk.first;

				if (m_lookupDependencySet.find(destChunkPos) != m_lookupDependencySet.end()) {
					m_lookupDependencySet[destChunkPos].erase(chunkPos);

					if (m_lookupDependencySet[destChunkPos].size() == 0) {
						m_lookupDependencySet.erase(destChunkPos);
					}
				}
			}

			m_dependencyMapList.erase(chunkPos);
		}

		if (m_patchedChunkSet.find(chunkPos) != m_patchedChunkSet.end())
			m_patchedChunkSet.erase(chunkPos);

		chunk->Clear();

		ReleaseChunkToPool(chunk);
	}
}

void ChunkManager::UpdatePatchChunkMap(Camera& camera)
{
	// TODO::
	// Mouse Picking을 PatchData로
	// Binary Greedy Meshing

	// move patch chunk to temp container for sort by camera distance
	std::vector<std::tuple<int, int, int>> tempPatchChunkPositionList;
	for (auto it = m_patchChunkMap.begin(); it != m_patchChunkMap.end(); ++it) {
		tempPatchChunkPositionList.push_back(it->first);
	}

	// sort temp container
	std::sort(tempPatchChunkPositionList.begin(), tempPatchChunkPositionList.end(),
		[&camera](auto& a, auto& b) {
			Vector3 aDiff = Utils::IntTupleToVector(a) - camera.GetPosition();
			Vector3 bDiff = Utils::IntTupleToVector(b) - camera.GetPosition();

			return aDiff.Length() < bDiff.Length();
		});

	// update patch chunk map, run patch thread
	for (auto& chunkPos : tempPatchChunkPositionList) {
		if (m_chunkMap.find(chunkPos) == m_chunkMap.end()) {
			m_patchChunkMap.erase(chunkPos);
			continue;
		}

		Chunk* chunk = m_chunkMap[chunkPos];
		if (!chunk) {
			m_patchChunkMap.erase(chunkPos);
			continue;
		}

		if (!chunk->IsLoaded()) {
			m_patchChunkMap.erase(chunkPos);
			continue;
		}

		if (chunk->IsPatching()) {
			continue;
		}

		if (m_patchFutures.size() == m_patchThreadCount) {
			continue;
		}

		const std::vector<ChunkPatchData>& chunkPatchDataList = m_patchChunkMap[chunkPos];

		ChunkLoadMemory* chunkLoadMemory = m_chunkLoadMemoryPool.back();
		m_chunkLoadMemoryPool.pop_back();

		m_patchFutures.push_back(
			std::make_pair(chunk, std::async(std::launch::async, &Chunk::Patch, chunk,
									  chunkPatchDataList, chunkLoadMemory)));
		
		m_patchChunkMap.erase(chunkPos);
	}

	// update gpu buffer for update
	for (auto it = m_patchFutures.begin(); it != m_patchFutures.end();) {
		if (it->second.wait_for(std::chrono::microseconds(0)) == std::future_status::ready) {
			Chunk* chunk = it->first;
			ChunkLoadMemory* chunkLoadMemory = it->second.get();

			chunk->UpdateCpuBufferCount();

			if (chunk->OnPatchDirtyFlag()) {
				UpdateChunkBuffer(chunk);
			}
			
			chunk->SetIsPatching(false);

			chunkLoadMemory->Clear();
			m_chunkLoadMemoryPool.push_back(chunkLoadMemory);

			it = m_patchFutures.erase(it);
		}
		else {
			++it;
		}
	}
}

void ChunkManager::UpdateRenderChunkList(Camera& camera, Light& light)
{
	m_renderChunkList.clear();
	m_renderMirrorChunkList.clear();
	m_renderShadowChunkList.clear();

	for (auto& p : m_chunkMap) {
		if (!p.second->IsLoaded())
			continue;
		
		if (p.second->IsEmpty()) {
			continue;
		}
		
		Vector3 chunkPos = p.second->GetPosition();
		if (FrustumCulling(chunkPos, camera, light, false, false)) {
			m_renderChunkList.push_back(p.second);
		}
		
		for (int i = 0; i < Light::CASCADE_NUM; ++i) {
			if (FrustumCulling(chunkPos, camera, light, false, true, i)) {
				m_renderShadowChunkList.push_back(p.second);
				break;
			}
		}

		Vector3 mirrorChunkPos = Vector3::Transform(chunkPos, camera.GetMirrorPlaneMatrix());
		if (FrustumCulling(mirrorChunkPos, camera, light, true, false)) {
			m_renderMirrorChunkList.push_back(p.second);
		}
	}
}

void ChunkManager::UpdateInstanceInfoList(Camera& camera)
{
	// clear all info
	for (int i = 0; i < Instance::INSTANCE_TYPE_COUNT; ++i)
		m_instanceInfoList[i].clear();

	// check instance in chunk managerList
	for (auto& c : m_renderChunkList) {
		if (c->IsUpdateRequired())
			continue;

		// check distance
		Vector3 chunkPosition = c->GetPosition();
		Vector3 chunkCenterPosition = chunkPosition + Vector3(Chunk::CHUNK_SIZE * 0.5);
		Vector3 diffPosition = chunkCenterPosition - camera.GetPosition();
		if (diffPosition.Length() > (float)Camera::LOD_RENDER_DISTANCE)
			continue;

		// set info
		const std::map<std::tuple<int, int, int>, Instance>& instanceMap = c->GetInstanceMap();
		for (auto& p : instanceMap) {
			InstanceInfoVertex info;

			info.texIndex = p.second.GetTextureIndex();

			info.instanceWorld = Matrix::CreateTranslation(p.second.GetWorldPosition()).Transpose();

			m_instanceInfoList[Instance::GetInstanceType(info.texIndex)].push_back(info);
		}
	}

	for (int i = 0; i < Instance::INSTANCE_TYPE_COUNT; ++i) {
		DXUtils::ResizeBuffer(m_instanceInfoBuffers[i], m_instanceInfoList[i],
			(UINT)D3D11_BIND_VERTEX_BUFFER, m_instanceInfoList[i].size() + 1024);
		DXUtils::UpdateBuffer(m_instanceInfoBuffers[i], m_instanceInfoList[i]);
	}
}

void ChunkManager::UpdateChunkConstant(float dt)
{
	for (auto& p : m_chunkMap) {
		if (p.second->IsLoaded() && p.second->IsUpdateRequired()) {
			p.second->Update(dt);

			ChunkConstantData tempConstantData;
			tempConstantData.world = p.second->GetConstantData().world.Transpose();

			if (m_constantBuffers[p.second->GetID()]) {
				DXUtils::UpdateConstantBuffer(
					m_constantBuffers[p.second->GetID()], tempConstantData);
			}

			if (p.second->GetPosition().y == p.second->GetOffsetPosition().y) {
				p.second->SetUpdateRequired(false);
			}
		}
	}
}

bool ChunkManager::FrustumCulling(
	Vector3 position, Camera& camera, Light& light, bool useMirror, bool useShadow, int index)
{
	Matrix invMat = Matrix();
	
	if (useShadow) {
		invMat = (light.GetViewMatrix() * light.GetProjectionMatrixFromCascade(index)).Invert();
	}
	else {
		invMat = (camera.GetViewMatrix() * camera.GetProjectionMatrix()).Invert();
	}

	std::vector<Vector3> worldPos = { Vector3::Transform(Vector3(-1.0f, 1.0f, 0.0f), invMat),
		Vector3::Transform(Vector3(1.0f, 1.0f, 0.0f), invMat),
		Vector3::Transform(Vector3(1.0f, -1.0f, 0.0f), invMat),
		Vector3::Transform(Vector3(-1.0f, -1.0f, 0.0f), invMat),
		Vector3::Transform(Vector3(-1.0f, 1.0f, 1.0f), invMat),
		Vector3::Transform(Vector3(1.0f, 1.0f, 1.0f), invMat),
		Vector3::Transform(Vector3(1.0f, -1.0f, 1.0f), invMat),
		Vector3::Transform(Vector3(-1.0f, -1.0f, 1.0f), invMat) };

	std::vector<Vector4> planes = { DirectX::XMPlaneFromPoints(
										worldPos[0], worldPos[1], worldPos[2]),
		DirectX::XMPlaneFromPoints(worldPos[7], worldPos[6], worldPos[5]),
		DirectX::XMPlaneFromPoints(worldPos[4], worldPos[5], worldPos[1]),
		DirectX::XMPlaneFromPoints(worldPos[3], worldPos[2], worldPos[6]),
		DirectX::XMPlaneFromPoints(worldPos[4], worldPos[0], worldPos[3]),
		DirectX::XMPlaneFromPoints(worldPos[1], worldPos[5], worldPos[6]) };

	float x = (float)Chunk::CHUNK_SIZE;
	float y = (float)Chunk::CHUNK_SIZE;
	float z = (float)Chunk::CHUNK_SIZE;
	if (useMirror)
		y *= -1;
	for (int i = 0; i < 6; ++i) {
		if (XMVectorGetX(XMPlaneDotCoord(planes[i], position)) < 0.0f)
			continue;
		if (XMVectorGetX(XMPlaneDotCoord(planes[i], position + Vector3(x, 0.0f, 0.0f))) <= 0.0f)
			continue;
		if (XMVectorGetX(XMPlaneDotCoord(planes[i], position + Vector3(0.0f, y, 0.0f))) <= 0.0f)
			continue;
		if (XMVectorGetX(XMPlaneDotCoord(planes[i], position + Vector3(x, y, 0.0f))) <= 0.0f)
			continue;
		if (XMVectorGetX(XMPlaneDotCoord(planes[i], position + Vector3(0.0f, 0.0f, z))) <= 0.0f)
			continue;
		if (XMVectorGetX(XMPlaneDotCoord(planes[i], position + Vector3(x, 0.0f, z))) <= 0.0f)
			continue;
		if (XMVectorGetX(XMPlaneDotCoord(planes[i], position + Vector3(0.0f, y, z))) <= 0.0f)
			continue;
		if (XMVectorGetX(XMPlaneDotCoord(planes[i], position + Vector3(x, y, z))) <= 0.0f)
			continue;
		return false;
	}

	return true;
}

void ChunkManager::UpdateChunkBuffer(Chunk* chunk)
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

Chunk* ChunkManager::GetChunkFromPool()
{
	if (!m_chunkPool.empty()) {
		Chunk* chunk = m_chunkPool.back();
		m_chunkPool.pop_back();
		return chunk;
	}
	return nullptr;
}

void ChunkManager::ReleaseChunkToPool(Chunk* chunk) { 
	m_chunkPool.push_back(chunk); 
}

bool ChunkManager::MakeInstanceVertexBuffer()
{
	std::vector<InstanceVertex> instanceVertices;
	std::vector<uint32_t> instanceIndices;

	// Instance Type 0 : CROSS
	MeshGenerator::CreateCrossInstanceMesh(instanceVertices, instanceIndices);
	if (!DXUtils::CreateVertexBuffer(
			m_instanceVertexBuffers[INSTANCE_TYPE::INSTANCE_CROSS], instanceVertices)) {
		std::cout << "failed create cross instance vertex buffer in chunk manager" << std::endl;
		return false;
	}
	if (!DXUtils::CreateIndexBuffer(
			m_instanceIndexBuffers[INSTANCE_TYPE::INSTANCE_CROSS], instanceIndices)) {
		std::cout << "failed create cross instance index buffer in chunk manager" << std::endl;
		return false;
	}
	instanceVertices.clear();
	instanceIndices.clear();


	// Instance Type 1 : FENCE
	MeshGenerator::CreateFenceInstanceMesh(instanceVertices, instanceIndices);
	if (!DXUtils::CreateVertexBuffer(
			m_instanceVertexBuffers[INSTANCE_TYPE::INSTANCE_FENCE], instanceVertices)) {
		std::cout << "failed create fence instance vertex buffer in chunk manager" << std::endl;
		return false;
	}
	if (!DXUtils::CreateIndexBuffer(
			m_instanceIndexBuffers[INSTANCE_TYPE::INSTANCE_FENCE], instanceIndices)) {
		std::cout << "failed create fence instance index buffer in chunk manager" << std::endl;
		return false;
	}
	instanceVertices.clear();
	instanceIndices.clear();


	// Instance Type 2 : SQUARE
	MeshGenerator::CreateSquareInstanceMesh(instanceVertices, instanceIndices);
	if (!DXUtils::CreateVertexBuffer(
			m_instanceVertexBuffers[INSTANCE_TYPE::INSTANCE_SQUARE], instanceVertices)) {
		std::cout << "failed create SQUARE instance vertex buffer in chunk manager" << std::endl;
		return false;
	}
	if (!DXUtils::CreateIndexBuffer(
			m_instanceIndexBuffers[INSTANCE_TYPE::INSTANCE_SQUARE], instanceIndices)) {
		std::cout << "failed create SQUARE instance index buffer in chunk manager" << std::endl;
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

bool ChunkManager::HasObjectAt(Vector3 position)
{
	const Instance* pickingInstance = ChunkManager::GetInstance()->GetInstanceByPosition(position);
	if (pickingInstance) {
		return true;
	}

	const Block* pickingBlock = ChunkManager::GetInstance()->GetBlockByPosition(position);
	if (pickingBlock && !Block::IsTransparency(pickingBlock->GetType())) {
		return true;
	}

	return false;
}

const Chunk* ChunkManager::GetChunkByPosition(Vector3 position)
{
	Vector3 chunkPos = Utils::CalcOffsetPos(position, Chunk::CHUNK_SIZE);

	auto iter = m_chunkMap.find(Utils::VectorToIntTuple(chunkPos));

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

	if (c != nullptr && c->IsLoaded())
	{
		Vector3 InstanceLocalPosition = position - Utils::CalcOffsetPos(position, Chunk::CHUNK_SIZE);
		return c->GetInstance(InstanceLocalPosition);
	}

	return nullptr;
}

void ChunkManager::RemoveBlockPatchAt(Vector3 position) 
{
	Vector3 chunkOffsetPos = Utils::CalcOffsetPos(position, Chunk::CHUNK_SIZE);
	std::tuple<int, int, int> chunkOffsetPosTuple = Utils::VectorToIntTuple(chunkOffsetPos);

	Vector3 blockLocalPos = position - chunkOffsetPos;

	ChunkPatchData patchData;
	patchData.localX = (int)blockLocalPos.x % Chunk::CHUNK_SIZE;
	patchData.localY = (int)blockLocalPos.y % Chunk::CHUNK_SIZE;
	patchData.localZ = (int)blockLocalPos.z % Chunk::CHUNK_SIZE;
	patchData.blockType =
		position.y <= Terrain::WATER_HEIGHT_LEVEL ? BLOCK_TYPE::BLOCK_WATER : BLOCK_TYPE::BLOCK_AIR;
	
	m_cameraPatchDataListMap[chunkOffsetPosTuple].push_back(patchData);

	if (m_chunkMap.find(chunkOffsetPosTuple) != m_chunkMap.end() &&
		m_chunkMap[chunkOffsetPosTuple]->IsLoaded()) {
		m_patchChunkMap[chunkOffsetPosTuple].push_back(patchData);
	}
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
	std::tuple<int, int, int> chunkOffsetPosTuple = Utils::VectorToIntTuple(chunkOffsetPos);

	Vector3 blockLocalPos = facePosition - chunkOffsetPos;

	ChunkPatchData patchData;
	patchData.localX = (int)blockLocalPos.x % Chunk::CHUNK_SIZE;
	patchData.localY = (int)blockLocalPos.y % Chunk::CHUNK_SIZE;
	patchData.localZ = (int)blockLocalPos.z % Chunk::CHUNK_SIZE;
	patchData.blockType = BLOCK_TYPE::BLOCK_GOLD;

	m_cameraPatchDataListMap[chunkOffsetPosTuple].push_back(patchData);

	if (m_chunkMap.find(chunkOffsetPosTuple) != m_chunkMap.end() &&
		m_chunkMap[chunkOffsetPosTuple]->IsLoaded()) {
		m_patchChunkMap[chunkOffsetPosTuple].push_back(patchData);
	}

}