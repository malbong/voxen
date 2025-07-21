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
	m_initThreadCount = std::clamp(std::thread::hardware_concurrency() - 2, 2u, 4u);
	for (unsigned int i = 0; i < m_initThreadCount; ++i) {
		m_chunkInitMemoryPool.push_back(new ChunkInitMemory());
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

void ChunkManager::Update(float dt, Camera& camera, Light& light)
{
	if (camera.m_isOnChunkDirtyFlag) {
		UpdateChunkList(camera.GetChunkPosition());
		camera.m_isOnChunkDirtyFlag = false;
	}

	UpdateLoadChunkList(camera);
	UpdateUnloadChunkList();
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

	Graphics::context->DrawIndexed((UINT)chunk->GetOpaqueIndices().size(), 0, 0);
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

	Graphics::context->DrawIndexed((UINT)chunk->GetSemiAlphaIndices().size(), 0, 0);
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

	Graphics::context->DrawIndexed((UINT)chunk->GetLowLodIndices().size(), 0, 0);
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

	Graphics::context->DrawIndexed((UINT)chunk->GetTransparencyIndices().size(), 0, 0);
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
	
	while (!m_loadChunkList.empty() && m_futures.size() < m_initThreadCount) {
		Chunk* chunk = m_loadChunkList.back();
		m_loadChunkList.pop_back();

		ChunkInitMemory* chunkInitMemory = m_chunkInitMemoryPool.back();
		m_chunkInitMemoryPool.pop_back();

		m_futures.push_back(std::make_pair(
			chunk, std::async(std::launch::async, &Chunk::Initialize, chunk, chunkInitMemory)));
	}

	for (auto it = m_futures.begin(); it != m_futures.end();) {
		if (it->second.wait_for(std::chrono::microseconds(0)) == std::future_status::ready) {
			ChunkInitMemory* chunkInitMemory = it->second.get();
			m_chunkInitMemoryPool.push_back(chunkInitMemory);

			Chunk* chunk = it->first;
			InitChunkBuffer(chunk);

			chunk->SetUpdateRequired(true);
			chunk->SetLoad(true);

			it = m_futures.erase(it);
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

		Vector3 pos = chunk->GetOffsetPosition();
		int x = (int)pos.x;
		int y = (int)pos.y;
		int z = (int)pos.z;
		m_chunkMap.erase(std::make_tuple(x, y, z));

		ReleaseChunkToPool(chunk);

		chunk->Clear();

		chunk->SetUpdateRequired(false);
		chunk->SetLoad(false);
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

		if (p.second->IsEmpty())
			continue;

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
		// check distance
		Vector3 chunkPosition = c->GetPosition();
		Vector3 chunkCenterPosition = chunkPosition + Vector3(Chunk::CHUNK_SIZE * 0.5);
		Vector3 diffPosition = chunkCenterPosition - camera.GetPosition();
		if (diffPosition.Length() > (float)Camera::LOD_RENDER_DISTANCE)
			continue;

		// set info
		const std::vector<Instance>& instanceList = c->GetInstanceList();
		for (auto& p : instanceList) {
			InstanceInfoVertex info;
			info.texIndex = p.GetTextureIndex();

			info.instanceWorld =
				(p.GetWorld() * Matrix::CreateTranslation(chunkPosition)).Transpose();

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

void ChunkManager::InitChunkBuffer(Chunk* chunk)
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
				(UINT)D3D11_BIND_VERTEX_BUFFER)) { // ¾à 8MB
			std::cout << "failed create instance info buffer in chunk manager" << std::endl;
			return false;
		}
	}

	return true;
}

const Chunk* ChunkManager::GetChunkByPosition(Vector3 position)
{
	Vector3 chunkPos = Utils::CalcOffsetPos(position, Chunk::CHUNK_SIZE);

	auto iter = m_chunkMap.find(std::make_tuple((int)chunkPos.x, (int)chunkPos.y, (int)chunkPos.z));

	if (iter == m_chunkMap.end())
		return nullptr;

	return iter->second;
}

const Block* ChunkManager::GetBlockByPosition(Vector3 position)
{
	const Chunk* c = GetChunkByPosition(position);

	if (c != nullptr && c->IsLoaded()) {
		Vector3 blockPos = position - Utils::CalcOffsetPos(position, Chunk::CHUNK_SIZE);
		return c->GetBlock(blockPos);
	}

	return nullptr;
}