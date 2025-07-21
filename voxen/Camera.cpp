#include "Camera.h"
#include "DXUtils.h"
#include "ChunkManager.h"
#include "MeshGenerator.h"

#include <algorithm>

Camera::Camera()
	: m_projFovAngleY(80.0f), m_nearZ(0.1f), m_farZ(1000.0f), m_aspectRatio(16.0f / 9.0f),
	  m_eyePos(0.0f, 0.0f, 0.0f), m_chunkPos(0.0f, 0.0f, 0.0f), m_forward(0.0f, 0.0f, 1.0f),
	  m_up(0.0f, 1.0f, 0.0f), m_right(1.0f, 0.0f, 0.0f), m_speed(20.0f), m_isUnderWater(false),
	  m_isOnConstantDirtyFlag(false), m_isOnChunkDirtyFlag(false), m_mouseSensitiveX(0.0005f),
	  m_mouseSensitiveY(0.001f), m_yaw(0.0f), m_pitch(0.0f), m_pickingBlock(nullptr)
{
}

Camera::~Camera() {}

bool Camera::Initialize(Vector3 pos)
{
	// Camera Data
	{
		m_eyePos = pos;
		m_chunkPos = Utils::CalcOffsetPos(m_eyePos, Chunk::CHUNK_SIZE);

		m_constantData.view = GetViewMatrix().Transpose();
		m_constantData.proj = GetProjectionMatrix().Transpose();
		m_constantData.invProj = GetProjectionMatrix().Invert().Transpose();
		m_constantData.eyePos = m_eyePos;
		m_constantData.eyeDir = m_forward;
		m_constantData.maxRenderDistance = (float)MAX_RENDER_DISTANCE;
		m_constantData.lodRenderDistance = (float)LOD_RENDER_DISTANCE;
		m_constantData.isUnderWater = m_isUnderWater;

		if (!DXUtils::CreateConstantBuffer(m_constantBuffer, m_constantData)) {
			std::cout << "failed create camera constant buffer" << std::endl;
			return false;
		}
	}

	// Mirror Plane
	{
		Plane mirrorPlane = Plane(Vector3(0.0f, 64.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f));
		m_mirrorPlaneMatrix = Matrix::CreateReflection(mirrorPlane);

		m_constantData.view = m_mirrorPlaneMatrix * GetViewMatrix();
		m_constantData.view = m_constantData.view.Transpose();

		if (!DXUtils::CreateConstantBuffer(m_mirrorConstantBuffer, m_constantData)) {
			std::cout << "failed create camera mirror constant buffer" << std::endl;
			return false;
		}
	}

	// Picking Block Buffer
	{
		MeshGenerator::CreatePickingBlockLineMesh(m_pickingBlockVertices, m_pickingBlockIndices);
		if (!DXUtils::CreateVertexBuffer(m_pickingBlockVertexBuffer, m_pickingBlockVertices)) {
			std::cout << "failed create picking block vertex buffer in camera" << std::endl;
			return false;
		}

		if (!DXUtils::CreateIndexBuffer(m_pickingBlockIndexBuffer, m_pickingBlockIndices)) {
			std::cout << "failed create picking block index buffer in camera" << std::endl;
			return false;
		}

		m_pickingBlockConstantData.world = Matrix();
		if (!DXUtils::CreateConstantBuffer(
				m_pickingBlockConstantBuffer, m_pickingBlockConstantData)) {
			std::cout << "failed create picking block constant buffer in camera" << std::endl;
			return false;
		}
	}

	return true;
}

void Camera::Update(float dt, bool keyPressed[256], LONG mouseDeltaX, LONG mouseDeltaY)
{
	UpdatePosition(keyPressed, dt);
	UpdateViewDirection(mouseDeltaX, mouseDeltaY);

	DDAPickingBlock();

	/////////////////////////////
	if (keyPressed['R']) {
		m_constantData.dummy.x = 0.0f;
		m_isOnConstantDirtyFlag = true;
	}
	else {
		m_constantData.dummy.x = 1.0f;
		m_isOnConstantDirtyFlag = true;
	}
	if (keyPressed['Q']) {
		m_constantData.dummy.z = 0.0f;
		m_isOnConstantDirtyFlag = true;
	}
	else {
		m_constantData.dummy.z = 1.0f;
		m_isOnConstantDirtyFlag = true;
	}
	/////////////////////////////

	if (m_isOnConstantDirtyFlag) {
		m_constantData.view = GetViewMatrix().Transpose();
		m_constantData.proj = GetProjectionMatrix().Transpose();
		m_constantData.invProj = GetProjectionMatrix().Invert().Transpose();
		m_constantData.eyePos = m_eyePos;
		m_constantData.eyeDir = m_forward;
		m_constantData.maxRenderDistance = (float)MAX_RENDER_DISTANCE;
		m_constantData.lodRenderDistance = (float)LOD_RENDER_DISTANCE;
		m_constantData.isUnderWater = m_isUnderWater;

		DXUtils::UpdateConstantBuffer(m_constantBuffer, m_constantData);

		m_constantData.view = m_mirrorPlaneMatrix * GetViewMatrix();
		m_constantData.view = m_constantData.view.Transpose();
		DXUtils::UpdateConstantBuffer(m_mirrorConstantBuffer, m_constantData);

		DXUtils::UpdateConstantBuffer(m_pickingBlockConstantBuffer, m_pickingBlockConstantData);

		m_isOnConstantDirtyFlag = false;
	}
}

void Camera::UpdatePosition(bool keyPressed[256], float dt)
{
	if (keyPressed['W']) {
		MoveForward(dt);
		m_isOnConstantDirtyFlag = true;
	}
	if (keyPressed['S']) {
		MoveForward(-dt);
		m_isOnConstantDirtyFlag = true;
	}
	if (keyPressed['D']) {
		MoveRight(dt);
		m_isOnConstantDirtyFlag = true;
	}
	if (keyPressed['A']) {
		MoveRight(-dt);
		m_isOnConstantDirtyFlag = true;
	}

	if (m_isOnConstantDirtyFlag) {
		SetIsUnderWater();

		Vector3 newChunkPos = Utils::CalcOffsetPos(m_eyePos, Chunk::CHUNK_SIZE);
		if (newChunkPos != m_chunkPos) {
			m_isOnChunkDirtyFlag = true;
			m_chunkPos = newChunkPos;
		}
	}
}

void Camera::UpdateViewDirection(LONG mouseDeltaX, LONG mouseDeltaY)
{
	if (mouseDeltaX == 0 && mouseDeltaY == 0)
		return;

	m_isOnConstantDirtyFlag = true;

	m_yaw += mouseDeltaX * m_mouseSensitiveX;
	m_pitch += mouseDeltaY * m_mouseSensitiveY;

	float thetaHorizontal = DirectX::XM_PI * m_yaw;
	float thetaVertical = DirectX::XM_PIDIV2 * m_pitch;
	// using Quaternion not Euler
	Vector3 basisX = Vector3(1.0f, 0.0f, 0.0f);
	Vector3 basisY = Vector3(0.0f, 1.0f, 0.0f);
	Vector3 basisZ = Vector3(0.0f, 0.0f, 1.0f);

	Quaternion qYaw =
		Quaternion(basisY * sinf(thetaHorizontal * 0.5f), cosf(thetaHorizontal * 0.5f));
	m_forward = Vector3::Transform(basisZ, Matrix::CreateFromQuaternion(qYaw));
	m_right = Vector3::Transform(basisX, Matrix::CreateFromQuaternion(qYaw));

	Quaternion qPitch =
		Quaternion(m_right * sinf(thetaVertical * 0.5f), cosf(thetaVertical * 0.5f));
	m_forward = Vector3::Transform(m_forward, Matrix::CreateFromQuaternion(qPitch));
	m_up = Vector3::Transform(basisY, Matrix::CreateFromQuaternion(qPitch));
}

void Camera::MoveForward(float dt) { m_eyePos += m_forward * m_speed * dt; }

void Camera::MoveRight(float dt) { m_eyePos += m_right * m_speed * dt; }

void Camera::SetIsUnderWater()
{
	m_isUnderWater = false;

	const Block* block = ChunkManager::GetInstance()->GetBlockByPosition(m_eyePos);
	if (block != nullptr && block->GetType() == BLOCK_TYPE::BLOCK_WATER) {
		m_isUnderWater = true;
		return;
	}

	float bias = 0.125f;
	block = ChunkManager::GetInstance()->GetBlockByPosition(m_eyePos - Vector3(0.0f, bias, 0.0f));
	if (block != nullptr && block->GetType() == BLOCK_TYPE::BLOCK_WATER) {
		m_isUnderWater = true;
		return;
	}
}

void Camera::DDAPickingBlock()
{
	// 현재 월드 위치
	int curX = (int)floorf(m_eyePos.x);
	int curY = (int)floorf(m_eyePos.y);
	int curZ = (int)floorf(m_eyePos.z);

	// DDA의 진행 방향
	int stepX = (m_forward.x > 0) ? 1 : -1;
	int stepY = (m_forward.y > 0) ? 1 : -1;
	int stepZ = (m_forward.z > 0) ? 1 : -1;

	// deltaX는 x가 1만큼 이동할 때의 벡터의 이동거리
	// 방향벡터와의 닮은비로 구함 -> 1 : deltaX == dirX : 1
	// 해당 방향의 방향벡터 성분이 없을 경우 최댓값을 두어 연산에 제외
	float deltaX = (m_forward.x != 0) ? fabs(1.0f / m_forward.x) : FLT_MAX;
	float deltaY = (m_forward.y != 0) ? fabs(1.0f / m_forward.y) : FLT_MAX;
	float deltaZ = (m_forward.z != 0) ? fabs(1.0f / m_forward.z) : FLT_MAX;

	// sideX는 현재 위치에서 Picking할 블록까지의 현재의 최댓값
	// 마찬가지로 성분 방향으로 거리를 구하고 닮은비로 구할 수 있음
	// sideX : (nextPos.x - pos.x) == deltaX : 1
	float sideX = (stepX > 0) ? (floorf(m_eyePos.x + 1) - m_eyePos.x) * deltaX
							  : (m_eyePos.x - floorf(m_eyePos.x)) * deltaX;
	float sideY = (stepY > 0) ? (floorf(m_eyePos.y + 1) - m_eyePos.y) * deltaY
							  : (m_eyePos.y - floorf(m_eyePos.y)) * deltaY;
	float sideZ = (stepZ > 0) ? (floorf(m_eyePos.z + 1) - m_eyePos.z) * deltaZ
							  : (m_eyePos.z - floorf(m_eyePos.z)) * deltaZ;

	while (min(min(sideX, sideY), sideZ) < 4.0f) {
		// 가장 가까운 side 찾기
		if (sideX < sideY && sideX < sideZ) {
			curX += stepX;
			sideX += deltaX;
		}
		else if (sideY < sideZ) {
			curY += stepY;
			sideY += deltaY;
		}
		else {
			curZ += stepZ;
			sideZ += deltaZ;
		}

		m_pickingBlock = ChunkManager::GetInstance()->GetBlockByPosition(
			Vector3((float)curX, (float)curY, (float)curZ));
		if (m_pickingBlock != nullptr && !Block::IsTransparency(m_pickingBlock->GetType())) {
			m_pickingBlockConstantData.world =
				Matrix::CreateTranslation(Vector3((float)curX, (float)curY, (float)curZ));
			m_pickingBlockConstantData.world = m_pickingBlockConstantData.world.Transpose();

			m_isOnConstantDirtyFlag = true;

			return;
		}
	}

	m_pickingBlock = nullptr;
}

void Camera::RenderPickingBlock() 
{ 
	Graphics::SetPipelineStates(Graphics::pickingBlockPSO);

	Graphics::context->OMSetRenderTargets(
		1, Graphics::basicMSRTV.GetAddressOf(), Graphics::basicDSV.Get());

	UINT stride = sizeof(PickingBlockVertex);
	UINT offset = 0;
	Graphics::context->IASetIndexBuffer(m_pickingBlockIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	Graphics::context->IASetVertexBuffers(
		0, 1, m_pickingBlockVertexBuffer.GetAddressOf(), &stride, &offset);

	Graphics::context->VSSetConstantBuffers(0, 1, m_pickingBlockConstantBuffer.GetAddressOf());

	Graphics::context->DrawIndexed((UINT)m_pickingBlockIndices.size(), 0, 0);
}