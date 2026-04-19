#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include "directxtk/SimpleMath.h"

#include "Chunk.h"

using namespace DirectX;
using namespace DirectX::SimpleMath;

class Camera {
public:
	static const int MAX_RENDER_DISTANCE = 320;
	static const int LOD_RENDER_DISTANCE = 260;

	Camera();
	~Camera();

	bool Initialize(Vector3 pos);
	void Update(float dt, bool keyPressed[256], LONG mouseDeltaX, LONG mouseDeltaY);
	void RenderPickingBlock();
	void RenderViewFrustum();

	inline Vector3 GetPosition() { return m_eyePos; }
	inline Vector3 GetChunkPosition() { return m_chunkPos; }
	inline Vector3 GetForward() { return m_forward; }
	inline Matrix GetViewMatrix() { return XMMatrixLookToLH(m_eyePos, m_forward, m_up); }
	inline Matrix GetProjectionMatrix()
	{
		return XMMatrixPerspectiveFovLH(
			XMConvertToRadians(m_projFovAngleY), m_aspectRatio, m_nearZ, m_farZ);
	}
	inline Matrix GetMirrorPlaneMatrix() { return m_mirrorPlaneMatrix; }
	inline bool IsUnderWater() { return m_isUnderWater; }

	inline bool HasPickingObject() { return m_hasPickingObject; }
	inline Vector3 GetPickingObjectPosition() { return m_pickingObjectPosition; }
	inline DIR GetPickingObjectFace() { return m_pickingObjectFace;}

	bool m_isOnConstantDirtyFlag;
	bool m_isOnChunkDirtyFlag;

	ComPtr<ID3D11Buffer> m_constantBuffer;
	ComPtr<ID3D11Buffer> m_mirrorConstantBuffer;
	ComPtr<ID3D11Buffer> m_cullingViewerConstantBuffer;


private:
	void UpdatePosition(bool keyPressed[256], float dt);
	void UpdateBasis();
	void UpdateViewDirection(bool keyPressed[256], float dt);
	void UpdateViewDirection(LONG mouseDeltaX, LONG mouseDeltaY);

	void MoveForward(float dt);
	void MoveRight(float dt);

	void CheckUnderWater();

	void DDAPickingBlock();

	float m_projFovAngleY;
	float m_nearZ;
	float m_farZ;
	float m_aspectRatio;

	Vector3 m_eyePos;
	Vector3 m_chunkPos;
	Vector3 m_forward;
	Vector3 m_up;
	Vector3 m_right;

	Vector3 m_cullingViewerPos;
	Vector3 m_cullingViewerForward;
	Vector3 m_cullingViewerUp;

	Matrix m_mirrorPlaneMatrix;

	float m_speed;

	bool m_isUnderWater;

	bool m_hasPickingObject;
	Vector3 m_pickingObjectPosition;
	DIR m_pickingObjectFace;

	float m_mouseSensitiveX;
	float m_mouseSensitiveY;
	float m_yaw;
	float m_pitch;

	Vector3 m_cullingViewerOffsetPos;

	CameraConstantData m_constantData;

	std::vector<PickingObjectVertex> m_pickingObjectVertices;
	std::vector<uint32_t> m_pickingObjectIndices;
	ChunkConstantData m_pickingObjectConstantData;

	ComPtr<ID3D11Buffer> m_pickingObjectVertexBuffer;
	ComPtr<ID3D11Buffer> m_pickingObjectIndexBuffer;
	ComPtr<ID3D11Buffer> m_pickingObjectConstantBuffer;

	std::vector<ViewFrustumVertex> m_viewFrustumVertices;
	std::vector<uint32_t> m_viewFrustumIndices;
	ComPtr<ID3D11Buffer> m_viewFrustumVertexBuffer;
	ComPtr<ID3D11Buffer> m_viewFrustumIndexBuffer;
};