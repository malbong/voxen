#include "Camera.h"
#include "DXUtils.h"
#include "ChunkManager.h"
#include "MeshGenerator.h"

#include <algorithm>

Camera::Camera()
	: m_projFovAngleY(70.0f), m_nearZ(0.1f), m_farZ(1000.0f), m_aspectRatio(16.0f / 9.0f),
	  m_eyePos(0.0f, 0.0f, 0.0f), m_chunkPos(0.0f, 0.0f, 0.0f), m_forward(0.0f, 0.0f, 1.0f),
	  m_up(0.0f, 1.0f, 0.0f), m_right(1.0f, 0.0f, 0.0f), m_speed(30.0f), m_isUnderWater(false),
	  m_isOnConstantDirtyFlag(false), m_mouseSensitiveX(0.0005f),
	  m_mouseSensitiveY(0.001f), m_yaw(0.0f), m_pitch(0.0f), m_hasPickingObject(false)
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
		m_constantData.invView = GetViewMatrix().Invert().Transpose();
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

	// Debug Camera for Frustum Culling
	{
		m_cullingViewerOffsetPos = Vector3(0.0f, 128.0f, -128.0f);
		m_cullingViewerPos = m_eyePos + m_cullingViewerOffsetPos;

		Quaternion qPitch = Quaternion(
			Vector3(1.0f, 0.0f, 0.0f) * sinf(XM_PIDIV2 * 0.25f), cosf(XM_PIDIV2 * 0.25f));
		m_cullingViewerForward = Vector3::Transform(Vector3(0.0f, 0.0f, 1.0f), Matrix::CreateFromQuaternion(qPitch));
		m_cullingViewerUp = Vector3::Transform(Vector3(0.0f, 1.0f, 0.0f), Matrix::CreateFromQuaternion(qPitch));

		m_constantData.view = XMMatrixLookToLH(m_cullingViewerPos, m_cullingViewerForward, m_cullingViewerUp);
		m_constantData.view = m_constantData.view.Transpose();

		if (!DXUtils::CreateConstantBuffer(m_cullingViewerConstantBuffer, m_constantData)) {
			std::cout << "failed create debug camera constant buffer" << std::endl;
			return false;
		}

		MeshGenerator::CreateViewFrustumLineMesh(m_viewFrustumVertices, m_viewFrustumIndices);
		if (!DXUtils::CreateVertexBuffer(m_viewFrustumVertexBuffer, m_viewFrustumVertices)) {
			std::cout << "failed create view frustum vertex buffer in camera" << std::endl;
			return false;
		}
		if (!DXUtils::CreateIndexBuffer(m_viewFrustumIndexBuffer, m_viewFrustumIndices)) {
			std::cout << "failed create view frustum index buffer in camera" << std::endl;
			return false;
		}
	}

	// Picking Block Buffer
	{
		MeshGenerator::CreatePickingBlockLineMesh(m_pickingObjectVertices, m_pickingObjectIndices);
		if (!DXUtils::CreateVertexBuffer(m_pickingObjectVertexBuffer, m_pickingObjectVertices)) {
			std::cout << "failed create picking block vertex buffer in camera" << std::endl;
			return false;
		}
		if (!DXUtils::CreateIndexBuffer(m_pickingObjectIndexBuffer, m_pickingObjectIndices)) {
			std::cout << "failed create picking block index buffer in camera" << std::endl;
			return false;
		}

		m_pickingObjectConstantData.world = Matrix();
		if (!DXUtils::CreateConstantBuffer(
				m_pickingObjectConstantBuffer, m_pickingObjectConstantData)) {
			std::cout << "failed create picking block constant buffer in camera" << std::endl;
			return false;
		}
	}

	return true;
}

void Camera::Update(float dt, bool keyToggled[256], bool keyPressed[256], LONG mouseDeltaX, LONG mouseDeltaY)
{
	UpdatePosition(keyToggled, keyPressed, dt);
	UpdateViewDirection(keyPressed, dt);
	UpdateViewDirection(mouseDeltaX, mouseDeltaY);

	DDAPickingBlock();

	if (m_isOnConstantDirtyFlag) {
		// basic
		m_constantData.view = GetViewMatrix().Transpose();
		m_constantData.proj = GetProjectionMatrix().Transpose();
		m_constantData.invView = GetViewMatrix().Invert().Transpose();
		m_constantData.invProj = GetProjectionMatrix().Invert().Transpose();
		m_constantData.eyePos = m_eyePos;
		m_constantData.eyeDir = m_forward;
		m_constantData.maxRenderDistance = (float)MAX_RENDER_DISTANCE;
		m_constantData.lodRenderDistance = (float)LOD_RENDER_DISTANCE;
		m_constantData.isUnderWater = m_isUnderWater;
		DXUtils::UpdateConstantBuffer(m_constantBuffer, m_constantData);

		// mirror
		m_constantData.view = m_mirrorPlaneMatrix * GetViewMatrix();
		m_constantData.view = m_constantData.view.Transpose();
		DXUtils::UpdateConstantBuffer(m_mirrorConstantBuffer, m_constantData);

		// culling viewer debug camera
		m_cullingViewerPos = m_eyePos + m_cullingViewerOffsetPos;
		m_constantData.view = XMMatrixLookToLH(m_cullingViewerPos, m_cullingViewerForward, m_cullingViewerUp);
		m_constantData.view = m_constantData.view.Transpose();
		DXUtils::UpdateConstantBuffer(m_cullingViewerConstantBuffer, m_constantData);

		// picking block
		DXUtils::UpdateConstantBuffer(m_pickingObjectConstantBuffer, m_pickingObjectConstantData);

		m_isOnConstantDirtyFlag = false;
	}
}

void Camera::UpdatePosition(bool keyToggled[256], bool keyPressed[256], float dt)
{
	if (keyToggled[0x71]) { // F1
		dt *= 0.25f;
	}

	if (keyToggled[0x70]) {
		m_eyePos = Vector3(158.0f, 128.0f, 50.0f);
		m_forward = Vector3(0.0f, 0.0f, 1.0f);
		m_right = Vector3(1.0f, 0.0f, 0.0f);
		m_up = Vector3(0.0f, 1.0f, 0.0f);

		keyToggled[0x70] = !keyToggled[0x70];

		m_isOnConstantDirtyFlag = true;
	}

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
		CheckUnderWater();

		Vector3 newChunkPos = Utils::CalcOffsetPos(m_eyePos, Chunk::CHUNK_SIZE);
		if (newChunkPos != m_chunkPos) {
			ChunkManager::GetInstance()->OnChunkUpdateDirtyFlag();

			m_chunkPos = newChunkPos;
		}
	}
}

void Camera::UpdateBasis()
{
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

void Camera::UpdateViewDirection(bool keyPressed[256], float dt) 
{ 
	if (keyPressed[VK_LEFT] || keyPressed[VK_RIGHT]) {
		m_isOnConstantDirtyFlag = true;

		int sign = keyPressed[VK_LEFT] ? -1 : 1;

		m_yaw += sign * dt * 0.25f;

		UpdateBasis();
	}
	
	if (keyPressed[VK_UP] || keyPressed[VK_DOWN]) {
		m_isOnConstantDirtyFlag = true;

		int sign = keyPressed[VK_UP] ? -1 : 1;

		m_pitch += sign * dt * 0.25f;

		UpdateBasis();
	}
}

void Camera::UpdateViewDirection(LONG mouseDeltaX, LONG mouseDeltaY)
{
	if (mouseDeltaX == 0 && mouseDeltaY == 0)
		return;

	m_isOnConstantDirtyFlag = true;

	m_yaw += mouseDeltaX * m_mouseSensitiveX;
	m_pitch += mouseDeltaY * m_mouseSensitiveY;

	UpdateBasis();
}

void Camera::MoveForward(float dt) { m_eyePos += m_forward * m_speed * dt; }

void Camera::MoveRight(float dt) { m_eyePos += m_right * m_speed * dt; }

void Camera::CheckUnderWater()
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
	// Picking 관련 멤버 초기화
	m_hasPickingObject = false;

	// 현재 월드 위치
	int curX = (int)floorf(m_eyePos.x);
	int curY = (int)floorf(m_eyePos.y);
	int curZ = (int)floorf(m_eyePos.z);

	// DDA의 진행 방향
	int stepX = (m_forward.x > 0) ? 1 : -1;
	int stepY = (m_forward.y > 0) ? 1 : -1;
	int stepZ = (m_forward.z > 0) ? 1 : -1;

	// 마주친 면
	DIR faceX = (stepX > 0) ? DIR::LEFT : DIR::RIGHT;
	DIR faceY = (stepY > 0) ? DIR::BOTTOM : DIR::TOP;
	DIR faceZ = (stepZ > 0) ? DIR::FRONT : DIR::BACK;

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

	DIR curFace = ANY;
	while (min(min(sideX, sideY), sideZ) < 4.0f) {
		// 가장 가까운 side 찾기
		if (sideX < sideY && sideX < sideZ) {
			curX += stepX;
			sideX += deltaX;
			curFace = faceX;
		}
		else if (sideY < sideZ) {
			curY += stepY;
			sideY += deltaY;
			curFace = faceY;
		}
		else {
			curZ += stepZ;
			sideZ += deltaZ;
			curFace = faceZ;
		}

		Vector3 position = Vector3((float)curX, (float)curY, (float)curZ);
		if (ChunkManager::GetInstance()->HasObjectAt(position)) {
			m_hasPickingObject = true;
			m_pickingObjectPosition = position;
			m_pickingObjectFace = curFace;

			m_pickingObjectConstantData.world =
				Matrix::CreateTranslation(m_pickingObjectPosition).Transpose();

			m_isOnConstantDirtyFlag = true;
			
			break;
		}
	}
}

void Camera::RenderPickingBlock() 
{ 
	Graphics::SetPipelineStates(Graphics::pickingBlockPSO);

	UINT stride = sizeof(PickingObjectVertex);
	UINT offset = 0;
	Graphics::context->IASetIndexBuffer(m_pickingObjectIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	Graphics::context->IASetVertexBuffers(
		0, 1, m_pickingObjectVertexBuffer.GetAddressOf(), &stride, &offset);

	Graphics::context->VSSetConstantBuffers(0, 1, m_pickingObjectConstantBuffer.GetAddressOf());

	Graphics::context->DrawIndexed((UINT)m_pickingObjectIndices.size(), 0, 0);
}

void Camera::RenderViewFrustum()
{
	Graphics::SetPipelineStates(Graphics::viewFrustumPSO);

	Graphics::context->OMSetRenderTargets(1, Graphics::cullingViewerRTV.GetAddressOf(), nullptr);

	UINT stride = sizeof(ViewFrustumVertex);
	UINT offset = 0;
	Graphics::context->IASetIndexBuffer(m_viewFrustumIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	Graphics::context->IASetVertexBuffers(
		0, 1, m_viewFrustumVertexBuffer.GetAddressOf(), &stride, &offset);

	Graphics::context->DrawIndexed((UINT)m_viewFrustumIndices.size(), 0, 0);
}