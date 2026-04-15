#include "Cloud.h"
#include "Utils.h"
#include "Terrain.h"
#include "DXUtils.h"
#include "MeshGenerator.h"
#include "Date.h"

#include <algorithm>
Cloud::Cloud()
	: m_speed(8.0f), m_offsetPosition(0.0f, 0.0f, 0.0f), m_worldPosition(0.0f, 0.0f, 0.0f),
	  m_height(192.0f), m_stride(sizeof(CloudVertex)), m_offset(0)
{
	m_map.resize(CLOUD_MAP_SIZE);
	for (int i = 0; i < CLOUD_MAP_SIZE; ++i) {
		m_map[i].resize(CLOUD_MAP_SIZE);
		std::fill(m_map[i].begin(), m_map[i].end(), false);
	}

	m_dataMap.resize(CLOUD_DATA_MAP_SIZE);
	for (int i = 0; i < CLOUD_DATA_MAP_SIZE; ++i) {
		m_dataMap[i].resize(CLOUD_DATA_MAP_SIZE);
		std::fill(m_dataMap[i].begin(), m_dataMap[i].end(), false);
	}
};

Cloud::~Cloud() {};

bool Cloud::Initialize(Vector3 cameraPosition)
{
	for (int i = 0; i < CLOUD_DATA_MAP_SIZE; ++i) {
		for (int j = 0; j < CLOUD_DATA_MAP_SIZE; ++j) {
			float noise1 = Utils::PerlinFbm((float)i / CLOUD_DATA_MAP_SIZE,
				(float)j / CLOUD_DATA_MAP_SIZE, CLOUD_DATA_MAP_SIZE * 0.125f, 3);
			float noise2 = Utils::PerlinFbm((float)i / CLOUD_DATA_MAP_SIZE,
				(float)j / CLOUD_DATA_MAP_SIZE, CLOUD_DATA_MAP_SIZE * 0.5f, 1);
			m_dataMap[i][j] = noise1 > 0.25f || noise2 > 0.45f;
		}
	}

	m_offsetPosition = Utils::CalcOffsetPos(cameraPosition, CLOUD_SCALE_SIZE);
	m_offsetPosition.y = 0.0f;
	m_worldPosition = m_offsetPosition;

	if (!BuildCloud())
		return false;

	return true;
}

void Cloud::Update(float dt, Vector3 cameraPosition)
{
	m_worldPosition.x -= m_speed * dt;

	Vector3 newWorldPosition = m_worldPosition;
	Vector3 diffPos = cameraPosition - m_worldPosition;

	if (diffPos.x > CLOUD_SCALE_SIZE) {
		newWorldPosition.x += CLOUD_SCALE_SIZE;
		m_offsetPosition.x += CLOUD_SCALE_SIZE;
	}
	if (diffPos.z > CLOUD_SCALE_SIZE) {
		newWorldPosition.z += CLOUD_SCALE_SIZE;
		m_offsetPosition.z += CLOUD_SCALE_SIZE;
	}
	if (diffPos.x < 0.0f) {
		newWorldPosition.x -= CLOUD_SCALE_SIZE;
		m_offsetPosition.x -= CLOUD_SCALE_SIZE;
	}
	if (diffPos.z < 0.0f) {
		newWorldPosition.z -= CLOUD_SCALE_SIZE;
		m_offsetPosition.z -= CLOUD_SCALE_SIZE;
	}

	if (newWorldPosition != m_worldPosition) {
		m_worldPosition = newWorldPosition;
		BuildCloud();
	}

	if (m_vertices.empty())
		return;

	Vector3 worldPosition = m_worldPosition + Vector3(0.0f, m_height, 0.0f);
	Matrix worldMatrix = Matrix::CreateScale(CLOUD_SCALE_SIZE, 4.0f, CLOUD_SCALE_SIZE) *
						 Matrix::CreateTranslation(worldPosition);
	m_constantData.world = worldMatrix.Transpose();

	DXUtils::UpdateConstantBuffer(m_constantBuffer, m_constantData);
}

void Cloud::Render()
{
	Graphics::context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	Graphics::context->IASetVertexBuffers(
		0, 1, m_vertexBuffer.GetAddressOf(), &m_stride, &m_offset);

	Graphics::context->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
	Graphics::context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());

	Graphics::context->DrawIndexed((UINT)m_indices.size(), 0, 0);
}

bool Cloud::BuildCloud()
{
	const int HALF_CLOUD_MAP_SIZE = CLOUD_MAP_SIZE / 2;
	const int GRID_OFFSET_X = (int)m_offsetPosition.x / CLOUD_SCALE_SIZE;
	const int GRID_OFFSET_Z = (int)m_offsetPosition.z / CLOUD_SCALE_SIZE;

	for (int i = 0; i < CLOUD_MAP_SIZE; ++i) {
		for (int j = 0; j < CLOUD_MAP_SIZE; ++j) {
			int x = (GRID_OFFSET_X + i - HALF_CLOUD_MAP_SIZE) % CLOUD_DATA_MAP_SIZE;
			int z = (GRID_OFFSET_Z + j - HALF_CLOUD_MAP_SIZE) % CLOUD_DATA_MAP_SIZE;

			if (x < 0)
				x += CLOUD_DATA_MAP_SIZE;
			if (z < 0)
				z += CLOUD_DATA_MAP_SIZE;

			m_map[i][j] = m_dataMap[x][z];
		}
	}

	m_vertices.clear();
	m_indices.clear();
	for (int i = 0; i < CLOUD_MAP_SIZE; ++i) {
		for (int j = 0; j < CLOUD_MAP_SIZE; ++j) {
			if (!m_map[i][j])
				continue;
			bool x_n = true, x_p = true, z_n = true, z_p = true;
			if (i > 0 && m_map[i - 1][j])
				x_n = false;
			if (i < CLOUD_MAP_SIZE - 1 && m_map[i + 1][j])
				x_p = false;
			if (j > 0 && m_map[i][j - 1])
				z_n = false;
			if (j < CLOUD_MAP_SIZE - 1 && m_map[i][j + 1])
				z_p = false;

			int x = i - (int)(CLOUD_MAP_SIZE * 0.5f);
			int z = j - (int)(CLOUD_MAP_SIZE * 0.5f);

			MeshGenerator::CreateCloudMesh(
				m_vertices, m_indices, x, 0, z, x_n, x_p, true, true, z_n, z_p);
		}
	}

	if (!m_vertices.empty()) {
		if (!DXUtils::ResizeBuffer(m_vertexBuffer, m_vertices, D3D11_BIND_VERTEX_BUFFER)) {
			std::cout << "failed resize vertex buffer in cloud" << std::endl;
			return false;
		}
		if (!DXUtils::UpdateBuffer(m_vertexBuffer, m_vertices)) {
			std::cout << "failed update vertex buffer in cloud" << std::endl;
			return false;
		}

		if (!DXUtils::ResizeBuffer(m_indexBuffer, m_indices, D3D11_BIND_INDEX_BUFFER)) {
			std::cout << "failed resize index buffer in cloud" << std::endl;
			return false;
		}
		if (!DXUtils::UpdateBuffer(m_indexBuffer, m_indices)) {
			std::cout << "failed update index buffer in cloud" << std::endl;
			return false;
		}

		if (!m_constantBuffer) {
			m_constantData.cloudScale = CLOUD_SCALE_SIZE * CLOUD_MAP_SIZE * 0.5;
			m_constantData.volumeColor = Vector3(1.0f, 1.0f, 1.0f);
			if (!DXUtils::CreateConstantBuffer(m_constantBuffer, m_constantData)) {
				std::cout << "failed create constant buffer in cloud" << std::endl;
				return false;
			}
		}
	}

	return true;
}