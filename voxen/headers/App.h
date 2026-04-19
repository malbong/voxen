#pragma once

#include <windows.h>
#include <d3d11.h>
#include <wrl.h>

#include "ChunkManager.h"
#include "Camera.h"
#include "Skybox.h"
#include "Cloud.h"
#include "Light.h"
#include "PostEffect.h"
#include "WorldMap.h"
#include "Date.h"

using namespace Microsoft::WRL;
using namespace DirectX;
using namespace DirectX::SimpleMath;

class App {

public:
	App();
	~App();

	LRESULT EventHandler(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
	bool Initialize();
	void Run();

	static const UINT APP_WIDTH = 1920;
	static const UINT APP_HEIGHT = 1080;
	static const UINT SHADOW_WIDTH = 3072;
	static const UINT SHADOW_HEIGHT = 1024;
	static const UINT MIRROR_WIDTH = APP_WIDTH / 2;
	static const UINT MIRROR_HEIGHT = APP_HEIGHT / 2;

private:
	bool InitWindow();
	bool InitDirectX();
	bool InitGUI();
	bool InitScene();

	void ImGuiFrame();
	void Update(float dt);
	void Render();

	void FillGBuffer();
	void MaskMSAAEdge();
	void RenderSSAO();
	void ShadingBasic();

	void ConvertToMSAA();

	void RenderSkybox();
	void RenderCloud();

	void RenderShadowMap();

	void RenderMirrorWorld();
	void RenderWaterPlane();

	void RenderPickingBlock();
	void RenderWorldMap();
	void RenderFogFilter();
	void RenderWaterFilter();
	void RenderBloom();

	void RenderFrustumCullingViewer();

	void LockCursor();
	void UnlockCursor();

	HWND m_hwnd;

	ComPtr<ID3D11Buffer> m_constantBuffer;
	AppConstantData m_constantData;

	bool m_keyPressed[256];
	bool m_keyToggled[256];

	LONG m_mouseDeltaX;
	LONG m_mouseDeltaY;

	bool m_mouseLeftDown;
	bool m_mouseRightDown;

	bool m_isActive;

	Date m_date;
	Camera m_camera;
	Skybox m_skybox;
	Cloud m_cloud;
	Light m_light;
	PostEffect m_postEffect;
	WorldMap m_worldMap;
};
