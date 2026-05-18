#include "App.h"
#include "Graphics.h"
#include "DXUtils.h"
#include "Terrain.h"
#include "SimpleQuadRenderer.h"

#include <iostream>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>


App* g_app = nullptr;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
	HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	return g_app->EventHandler(hwnd, uMsg, wParam, lParam);
}

App::App()
	: m_hwnd(), m_constantBuffer(nullptr), m_constantData(), m_camera(), m_skybox(), m_cloud(),
	  m_light(), m_postEffect(),
	  m_keyPressed{
		  false,
	  },
	  m_keyToggled{
		  false,
	  },
	  m_mouseDeltaX(0), m_mouseDeltaY(0), m_isActive(false)
{
	g_app = this;
}

App::~App()
{
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	DestroyWindow(m_hwnd);
}

LRESULT App::EventHandler(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam))
		return true;

	switch (uMsg) {

	case WM_DESTROY: {
		UnlockCursor();
		PostQuitMessage(0);
		break;
	}

	case WM_ACTIVATEAPP: {
		if (LOWORD(wParam) == false) { // inactive
			UnlockCursor();
		}
		else {
			LockCursor();
		}

		break;
	}

	case WM_ENTERSIZEMOVE: {
		UnlockCursor();
		break;
	}

	case WM_EXITSIZEMOVE: {
		LockCursor();
		break;
	}

	case WM_KEYDOWN: {
		if (UINT(wParam) == VK_ESCAPE) {
			DestroyWindow(hwnd);
			break;
		}

		m_keyPressed[UINT(wParam)] = true;
		m_keyToggled[UINT(wParam)] = !m_keyToggled[UINT(wParam)];

		break;
	}

	case WM_KEYUP: {
		m_keyPressed[UINT(wParam)] = false;
		break;
	}

	case WM_INPUT: {
		if (!m_isActive)
			break;

		UINT dwSize;
		GetRawInputData((HRAWINPUT)lParam, RID_INPUT, nullptr, &dwSize, sizeof(RAWINPUTHEADER));
		LPBYTE lpb = new BYTE[dwSize];
		if (lpb == NULL) {
			break;
		}

		if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, lpb, &dwSize, sizeof(RAWINPUTHEADER)) ==
			dwSize) {
			RAWINPUT* raw = (RAWINPUT*)lpb;

			if (raw->header.dwType == RIM_TYPEMOUSE) {
				m_mouseDeltaX += raw->data.mouse.lLastX;
				m_mouseDeltaY += raw->data.mouse.lLastY;

				USHORT mouseButtonFlags = raw->data.mouse.usButtonFlags;
				m_mouseLeftDown = (mouseButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN);
				m_mouseRightDown = (mouseButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN);
			}
		}
		delete[] lpb;
		break;
	}

	default:
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}

	return true;
}

bool App::Initialize()
{
	if (!InitWindow())
		return false;

	if (!InitDirectX())
		return false;

	if (!InitGUI())
		return false;

	if (!InitScene())
		return false;

	return true;
}

bool App::InitWindow()
{
	// Window 초기화
	{
		const wchar_t CLASS_NAME[] = L"Voxen Class";
		HINSTANCE hInstance = GetModuleHandle(0);

		WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WindowProc, 0L, 0L, GetModuleHandle(NULL),
			NULL, NULL, NULL, NULL,
			CLASS_NAME, // lpszClassName, L-string
			NULL };

		if (!RegisterClassEx(&wc))
			return false;

		RECT wr = { 0, 0, (LONG)APP_WIDTH, (LONG)APP_HEIGHT };
		AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, false);

		DWORD dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
		m_hwnd = CreateWindow(wc.lpszClassName, L"Voxen", dwStyle, 100, 100, wr.right - wr.left,
			wr.bottom - wr.top, NULL, NULL, hInstance, NULL);

		if (m_hwnd == NULL)
			return false;

		ShowWindow(m_hwnd, SW_SHOWDEFAULT);
		UpdateWindow(m_hwnd);
	}

	// RAW INPUT 등록
	{
		RAWINPUTDEVICE rid;
		rid.usUsagePage = 0x01; // HID_USAGE_PAGE_GENERIC
		rid.usUsage = 0x02;		// HID_USAGE_GENERIC_MOUSE
		rid.dwFlags = RIDEV_INPUTSINK;
		rid.hwndTarget = m_hwnd;

		RegisterRawInputDevices(&rid, 1, sizeof(rid));
	}

	return true;
}

bool App::InitDirectX()
{
	DXGI_FORMAT pixelFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	if (!Graphics::InitGraphicsCore(pixelFormat, m_hwnd)) {
		return false;
	}

	if (!Graphics::InitGraphicsBuffer()) {
		return false;
	}

	if (!Graphics::InitGraphicsState()) {
		return false;
	}

	Graphics::InitGraphicsPSO();

	return true;
}

bool App::InitGUI()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	(void)io;
	io.DisplaySize = ImVec2(float(APP_WIDTH), float(APP_HEIGHT));
	ImGui::StyleColorsLight();

	// Setup Platform/Renderer backends
	if (!ImGui_ImplDX11_Init(Graphics::device.Get(), Graphics::context.Get())) {
		return false;
	}

	if (!ImGui_ImplWin32_Init(m_hwnd)) {
		return false;
	}

	return true;
}

bool App::InitScene()
{
	if (!m_camera.Initialize(
			Vector3(0.0f, 162.0f, -0.0f))) // snow Vector3(-500.0f, 128.0f, 2800.0f)
		return false;

	if (!ChunkManager::GetInstance()->Initialize(m_camera.GetChunkPosition()))
		return false;

	if (!m_skybox.Initialize(550.0f))
		return false;

	if (!m_cloud.Initialize(m_camera.GetPosition()))
		return false;

	if (!m_light.Initialize())
		return false;

	if (!m_postEffect.Initialize())
		return false;

	if (!m_worldMap.Initialize(m_camera.GetPosition()))
		return false;

	if (!m_date.Initialize()) {
		return false;
	}

	if (!m_SSAO.Initialize()) {
		return false;
	}

	m_constantData.appWidth = APP_WIDTH;
	m_constantData.appHeight = APP_HEIGHT;
	m_constantData.mirrorWidth = MIRROR_WIDTH;
	m_constantData.mirrorHeight = MIRROR_HEIGHT;
	if (!DXUtils::CreateConstantBuffer(m_constantBuffer, m_constantData)) {
		std::cout << "failed create constant buffer in app" << std::endl;
		return false;
	}

	return true;
}

void App::Run()
{
	MSG msg = { 0 };
	while (WM_QUIT != msg.message) {
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else {
			ImGuiFrame();

			Update(ImGui::GetIO().DeltaTime);
			Render();

			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData()); // GUI 렌더링

			Graphics::swapChain->Present(1, 0);
		}
	}
}

void App::ImGuiFrame()
{
	ImGui_ImplDX11_NewFrame(); // GUI 프레임 시작
	ImGui_ImplWin32_NewFrame();

	ImGui::NewFrame(); // 어떤 것들을 렌더링 할지 기록 시작
	ImGui::Begin("Scene Control");
	ImGui::Text("Average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate,
		ImGui::GetIO().Framerate);

	ImGui::Text("P: Pause");
	ImGui::Text("F: Light Move");
	ImGui::Text("M: World Map");
	ImGui::Text("V: Frustum Culling View");
	ImGui::Text("R: Reflection World View");
	ImGui::Text("G: GBuffer View");
	ImGui::Text("E: Edge View");
	ImGui::Text("O: SSAO View");
	ImGui::Text("B: Toggle SSAO Blur[Bilateral vs Gaussian]");
	ImGui::Text("");

	float worldX = m_camera.GetPosition().x;
	float worldY = m_camera.GetPosition().y;
	float worldZ = m_camera.GetPosition().z;
	ImGui::Text("x : %.4f y : %.4f z : %.4f", worldX, worldY, worldZ);

	float c = Terrain::GetContinentalness((int)worldX, (int)worldZ);
	float e = Terrain::GetErosion((int)worldX, (int)worldZ);
	float pv = Terrain::GetPeaksValley((int)worldX, (int)worldZ);
	ImGui::Text("C : %.2f | E : %.2f | PV : %.2f", c, e, pv);

	float t = Terrain::GetTemperature((int)worldX, (int)worldZ);
	float h = Terrain::GetHumidity((int)worldX, (int)worldZ);
	float b = Biome::GetBiomeTerrainHeight(c, e, pv, t, h);
	ImGui::Text("B : %.2f | T : %.2f | H : %.2f", b, t, h);
	Biome::GetBiomeTerrainHeight(c, e, pv, t, h);

	BIOME_TYPE biomeType = Biome::GetBiomeType(c, e, t, h, (int)worldX, (int)worldZ);
	const char* biomeString = nullptr;
	switch (biomeType) {
	case BIOME_TYPE::BIOME_OCEAN:
		biomeString = "BIOME_OCEAN";
		break;

	case BIOME_TYPE::BIOME_TUNDRA:
		biomeString = "BIOME_TUNDRA";
		break;

	case BIOME_TYPE::BIOME_TAIGA:
		biomeString = "BIOME_TAIGA";
		break;

	case BIOME_TYPE::BIOME_PLAINS:
		biomeString = "BIOME_PLAINS";
		break;

	case BIOME_TYPE::BIOME_SWAMP:
		biomeString = "BIOME_SWAMP";
		break;

	case BIOME_TYPE::BIOME_FOREST:
		biomeString = "BIOME_FOREST";
		break;

	case BIOME_TYPE::BIOME_SHRUBLAND:
		biomeString = "BIOME_SHRUBLAND";
		break;

	case BIOME_TYPE::BIOME_DESERT:
		biomeString = "BIOME_DESERT";
		break;

	case BIOME_TYPE::BIOME_RAINFOREST:
		biomeString = "BIOME_RAINFOREST";
		break;

	case BIOME_TYPE::BIOME_SEASONFOREST:
		biomeString = "BIOME_SEASONFOREST";
		break;

	case BIOME_TYPE::BIOME_SAVANNA:
		biomeString = "BIOME_SAVANNA";
		break;

	case BIOME_TYPE::BIOME_SNOWY_TAIGA:
		biomeString = "BIOME_SNOWY_TAIGA";
		break;

	default:
		biomeString = "BIOME_NONE";
		break;
	}
	ImGui::Text("BIOME: %s", biomeString);

	ImGui::End();
	ImGui::Render(); // 렌더링할 것들 기록 끝
}

void App::Update(float dt)
{
	if (m_keyToggled['P']) {
		return;
	}

	if (m_keyToggled['F']) {
		m_date.Update(dt);
	}
	else {
		m_date.Update(0.0f);
	}

	m_camera.Update(dt, m_keyPressed, m_mouseDeltaX, m_mouseDeltaY);

	m_postEffect.Update(dt, m_camera.IsUnderWater());

	ChunkManager::GetInstance()->Update(dt, m_camera, m_light, m_mouseLeftDown, m_mouseRightDown);

	m_worldMap.Update(m_camera.GetPosition());

	m_skybox.Update(m_date.GetDateTime());

	m_light.Update(m_date.GetDateTime(), m_camera);

	m_cloud.Update(dt, m_camera.GetPosition());

	m_mouseDeltaX = 0;
	m_mouseDeltaY = 0;
	m_mouseLeftDown = false;
	m_mouseRightDown = false;
}

void App::SetGlobalConstantBuffer()
{
	std::vector<ID3D11Buffer*> ppConstantBuffers;
	ppConstantBuffers.push_back(m_constantBuffer.Get());
	ppConstantBuffers.push_back(m_camera.m_constantBuffer.Get());
	ppConstantBuffers.push_back(m_skybox.m_constantBuffer.Get());
	ppConstantBuffers.push_back(m_light.m_lightConstantBuffer.Get());
	ppConstantBuffers.push_back(m_light.m_shadowConstantBuffer.Get());
	ppConstantBuffers.push_back(m_date.m_constantBuffer.Get());

	Graphics::context->VSSetConstantBuffers(
		7, (UINT)ppConstantBuffers.size(), ppConstantBuffers.data());
	Graphics::context->PSSetConstantBuffers(
		7, (UINT)ppConstantBuffers.size(), ppConstantBuffers.data());
}

void App::Render()
{
	SetGlobalConstantBuffer();

	// 0. Shadow Map
	{
		RenderShadowMap();
	}

	// 1. Deferred Render Pass
	{
		FillGBuffer();
		MaskMSAAEdge();
		RenderSSAO();
		ShadingBasic();
	}

	// 2. No-MSAA to MSAA Texture
	{
		ConvertToMSAA();
	}

	// 3. Picking Block
	{
		if (m_camera.HasPickingObject()) {
			RenderPickingBlock();
		}
	}

	// 4. Forward Render Pass MSAA
	{
		if (m_camera.IsUnderWater()) {
			RenderFogFilter();
			RenderSkybox();
			RenderCloud();
			RenderWaterPlane();
		}
		else {
			RenderMirrorWorld();
			RenderWaterPlane();
			RenderFogFilter();
			RenderSkybox();
			RenderCloud();
		}
	}

	// 5. Post Effect
	{
		Graphics::context->ResolveSubresource(Graphics::basicBuffer.Get(), 0,
			Graphics::basicMSBuffer.Get(), 0, DXGI_FORMAT_R16G16B16A16_FLOAT);

		if (m_camera.IsUnderWater()) {
			RenderWaterFilter();
		}

		RenderBloom();
	}

	// 6. Biome Map
	{
		if (m_keyToggled['M']) {
			RenderWorldMap();
		}
	}

	// 7. Debug Camera for Frustum Culling
	{
		if (m_keyToggled['V']) {
			RenderFrustumCullingViewer();
		}
	}

	// 8. Water Reflection World View
	{
		if (m_keyToggled['R']) {
			RenderReflectionWorld();
		}
	}

	// 9. G-Buffer View
	{
		if (m_keyToggled['G']) {
			RenderGBufferViewer();
		}
	}

	// 10. SSAO View
	{
		if (m_keyToggled['O']) {
			RenderSSAOViewer();
		}
	}

}

void App::FillGBuffer()
{
	float clearColor[4] = { 0.0f, 0.0f, 0.0f, -1.0f };
	Graphics::context->ClearRenderTargetView(Graphics::normalEdgeRTV.Get(), clearColor);
	Graphics::context->ClearRenderTargetView(Graphics::positionRTV.Get(), clearColor);
	Graphics::context->ClearRenderTargetView(Graphics::albedoRTV.Get(), clearColor);
	Graphics::context->ClearRenderTargetView(Graphics::coverageRTV.Get(), clearColor);
	Graphics::context->ClearRenderTargetView(Graphics::merRTV.Get(), clearColor);
	Graphics::context->ClearDepthStencilView(
		Graphics::basicDSV.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	std::vector<ID3D11RenderTargetView*> ppRTVs;
	ppRTVs.push_back(Graphics::normalEdgeRTV.Get());
	ppRTVs.push_back(Graphics::positionRTV.Get());
	ppRTVs.push_back(Graphics::albedoRTV.Get());
	ppRTVs.push_back(Graphics::coverageRTV.Get());
	ppRTVs.push_back(Graphics::merRTV.Get());
	Graphics::context->OMSetRenderTargets(
		(UINT)ppRTVs.size(), ppRTVs.data(), Graphics::basicDSV.Get());

	std::vector<ID3D11ShaderResourceView*> ppSRVs;
	ppSRVs.push_back(Graphics::blockAtlasMapSRV.Get());
	ppSRVs.push_back(Graphics::normalAtlasMapSRV.Get());
	ppSRVs.push_back(Graphics::merAtlasMapSRV.Get());
	ppSRVs.push_back(Graphics::grassColorMapSRV.Get());
	ppSRVs.push_back(Graphics::foliageColorMapSRV.Get());
	ppSRVs.push_back(Graphics::climateMapSRV.Get());
	Graphics::context->PSSetShaderResources(0, (UINT)ppSRVs.size(), ppSRVs.data());

	ChunkManager::GetInstance()->RenderBasic(m_camera.GetPosition());
}

void App::MaskMSAAEdge()
{
	Graphics::context->ClearDepthStencilView(
		Graphics::deferredDSV.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	Graphics::context->OMSetRenderTargets(0, nullptr, Graphics::deferredDSV.Get());

	std::vector<ID3D11ShaderResourceView*> ppSRVs;
	ppSRVs.push_back(Graphics::normalEdgeSRV.Get());
	ppSRVs.push_back(Graphics::positionSRV.Get());
	Graphics::context->PSSetShaderResources(0, (UINT)ppSRVs.size(), ppSRVs.data());

	Graphics::SetPipelineStates(Graphics::edgeMaskingPSO);
	SimpleQuadRenderer::GetInstance()->Render();
}

void App::RenderSSAO()
{
	// SSAO
	{
		float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		Graphics::context->ClearRenderTargetView(Graphics::ssaoRTV.Get(), clearColor);

		Graphics::context->OMSetRenderTargets(
			1, Graphics::ssaoRTV.GetAddressOf(), Graphics::deferredDSV.Get());

		m_SSAO.Render();
	}

	// Blur
	{
		int blurCount = 3;

		if (!m_keyToggled['B']) {
			m_postEffect.BlurBilateral(blurCount, Graphics::ssaoSRV, Graphics::ssaoRTV,
				Graphics::ssaoBlurSRV, Graphics::ssaoBlurRTV);
		}
		else {
			m_postEffect.BlurGaussian(blurCount, Graphics::ssaoSRV, Graphics::ssaoRTV,
				Graphics::ssaoBlurSRV, Graphics::ssaoBlurRTV);
		}
	}
}

void App::ShadingBasic()
{
	Graphics::context->OMSetRenderTargets(
		1, Graphics::basicRTV.GetAddressOf(), Graphics::deferredDSV.Get());

	std::vector<ID3D11ShaderResourceView*> ppSRVs;
	ppSRVs.push_back(Graphics::normalEdgeSRV.Get());
	ppSRVs.push_back(Graphics::positionSRV.Get());
	ppSRVs.push_back(Graphics::albedoSRV.Get());
	ppSRVs.push_back(Graphics::merSRV.Get());
	ppSRVs.push_back(Graphics::ssaoSRV.Get());
	Graphics::context->PSSetShaderResources(0, (UINT)ppSRVs.size(), ppSRVs.data());

	Graphics::context->PSSetShaderResources(10, 1, Graphics::brdfSRV.GetAddressOf());
	Graphics::context->PSSetShaderResources(11, 1, Graphics::shadowSRV.GetAddressOf());

	Graphics::SetPipelineStates(Graphics::shadingBasicPSO);
	SimpleQuadRenderer::GetInstance()->Render();

	Graphics::SetPipelineStates(
		m_keyToggled['E'] ? Graphics::shadingBasicEdgeHighlightPSO : Graphics::shadingBasicEdgePSO);
	SimpleQuadRenderer::GetInstance()->Render();
		

	ID3D11ShaderResourceView* nullSRV[2] = { nullptr, nullptr };
	Graphics::context->PSSetShaderResources(10, 2, nullSRV);
}

void App::ConvertToMSAA()
{
	Graphics::context->OMSetRenderTargets(1, Graphics::basicMSRTV.GetAddressOf(), nullptr);

	Graphics::context->PSSetShaderResources(0, 1, Graphics::basicSRV.GetAddressOf());

	Graphics::SetPipelineStates(Graphics::samplingPSO);
	SimpleQuadRenderer::GetInstance()->Render();
}

void App::RenderSkybox()
{
	Graphics::context->OMSetRenderTargets(
		1, Graphics::basicMSRTV.GetAddressOf(), Graphics::basicDSV.Get());

	Graphics::SetPipelineStates(Graphics::skyboxPSO);
	m_skybox.Render();
}

void App::RenderCloud()
{
	Graphics::context->OMSetRenderTargets(
		1, Graphics::basicMSRTV.GetAddressOf(), Graphics::basicDSV.Get());

	Graphics::SetPipelineStates(Graphics::cloudPSO);
	m_cloud.Render();
}

void App::RenderMirrorWorld()
{
	Graphics::context->RSSetViewports(1, &Graphics::mirrorWorldViewport);

	const FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	Graphics::context->ClearRenderTargetView(Graphics::mirrorDepthRTV.Get(), clearColor);
	Graphics::context->ClearRenderTargetView(Graphics::mirrorWorldRTV.Get(), clearColor);
	Graphics::context->ClearDepthStencilView(
		Graphics::mirrorWorldDSV.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	// stencil and water depth
	{
		Graphics::context->OMSetRenderTargets(
			1, Graphics::mirrorDepthRTV.GetAddressOf(), Graphics::mirrorWorldDSV.Get());
		Graphics::context->PSSetShaderResources(0, 1, Graphics::basicDepthSRV.GetAddressOf());
		Graphics::SetPipelineStates(Graphics::mirrorMaskingPSO);
		ChunkManager::GetInstance()->RenderTransparency();
	}

	// mirror world render in stencil
	{
		Graphics::context->OMSetRenderTargets(
			1, Graphics::mirrorWorldRTV.GetAddressOf(), Graphics::mirrorWorldDSV.Get());

		// mirror sky shader
		{
			Graphics::SetPipelineStates(Graphics::skyboxMirrorPSO);
			m_skybox.Render();
		}

		// mirror cloud
		{
			// mirror constant buffer: mirror plane view matrix
			Graphics::context->VSSetConstantBuffers(
				8, 1, m_camera.m_mirrorConstantBuffer.GetAddressOf());
			Graphics::SetPipelineStates(Graphics::cloudMirrorPSO);
			m_cloud.Render();
		}

		// mirror low lod world
		{
			std::vector<ID3D11ShaderResourceView*> ppSRVs;
			ppSRVs.push_back(Graphics::blockAtlasMapSRV.Get());
			ppSRVs.push_back(Graphics::normalAtlasMapSRV.Get());
			ppSRVs.push_back(Graphics::merAtlasMapSRV.Get());
			ppSRVs.push_back(Graphics::grassColorMapSRV.Get());
			ppSRVs.push_back(Graphics::foliageColorMapSRV.Get());
			ppSRVs.push_back(Graphics::climateMapSRV.Get());
			ppSRVs.push_back(Graphics::mirrorDepthSRV.Get());
			Graphics::context->PSSetShaderResources(0, (UINT)ppSRVs.size(), ppSRVs.data());
			ChunkManager::GetInstance()->RenderMirrorWorld();
		}
	}

	// blur mirror world
	{
		m_postEffect.BlurGaussian(2, Graphics::mirrorWorldSRV, Graphics::mirrorWorldRTV,
			Graphics::mirrorBlurSRV, Graphics::mirrorBlurRTV);
	}

	// 원래의 글로벌로 두기
	Graphics::context->VSSetConstantBuffers(8, 1, m_camera.m_constantBuffer.GetAddressOf());
	Graphics::context->RSSetViewports(1, &Graphics::basicViewport);
}

void App::RenderWaterPlane()
{
	Graphics::context->OMSetRenderTargets(
		1, Graphics::basicMSRTV.GetAddressOf(), Graphics::basicDSV.Get());

	Graphics::context->CopyResource(
		Graphics::copyForwardRenderBuffer.Get(), Graphics::basicMSBuffer.Get());

	std::vector<ID3D11ShaderResourceView*> ppSRVs;
	ppSRVs.push_back(Graphics::copyForwardSRV.Get());
	ppSRVs.push_back(Graphics::mirrorWorldSRV.Get());
	ppSRVs.push_back(Graphics::positionSRV.Get());
	ppSRVs.push_back(Graphics::waterColorMapSRV.Get());
	ppSRVs.push_back(Graphics::climateMapSRV.Get());
	ppSRVs.push_back(Graphics::waterStillAtlasMapSRV.Get());
	ppSRVs.push_back(Graphics::waterStillNormalAtlasMapSRV.Get());
	Graphics::context->PSSetShaderResources(0, (UINT)ppSRVs.size(), ppSRVs.data());
	Graphics::context->PSSetShaderResources(10, 1, Graphics::brdfSRV.GetAddressOf());
	Graphics::context->PSSetShaderResources(11, 1, Graphics::shadowSRV.GetAddressOf());

	Graphics::SetPipelineStates(Graphics::waterPlanePSO);
	ChunkManager::GetInstance()->RenderTransparency();

	ID3D11ShaderResourceView* nullSRV[2] = { nullptr, nullptr };
	Graphics::context->PSSetShaderResources(10, 2, nullSRV);
}

void App::RenderShadowMap()
{
	Graphics::context->RSSetViewports(Light::CASCADE_NUM, Graphics::shadowViewports);

	Graphics::context->OMSetRenderTargets(0, nullptr, Graphics::shadowDSV.Get());

	Graphics::context->ClearDepthStencilView(Graphics::shadowDSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

	Graphics::context->GSSetConstantBuffers(0, 1, m_light.m_shadowConstantBuffer.GetAddressOf());

	// Basic Shadow Map
	{
		Graphics::SetPipelineStates(Graphics::basicShadowPSO);
		ChunkManager::GetInstance()->RenderBasicShadowMap();
	}

	// Instance Shadow Map
	{
		Graphics::context->PSSetShaderResources(0, 1, Graphics::blockAtlasMapSRV.GetAddressOf());

		Graphics::SetPipelineStates(Graphics::instanceShadowPSO);
		ChunkManager::GetInstance()->RenderInstance();
	}

	Graphics::context->RSSetViewports(1, &Graphics::basicViewport);
}

void App::RenderPickingBlock() 
{ 
	Graphics::context->OMSetRenderTargets(
		1, Graphics::basicMSRTV.GetAddressOf(), Graphics::basicDSV.Get());

	m_camera.RenderPickingBlock(); 
}

void App::RenderWorldMap() 
{ 
	Graphics::context->OMSetRenderTargets(1, Graphics::backBufferRTV.GetAddressOf(), nullptr);

	m_worldMap.RenderMap(); 
}

void App::RenderFogFilter() 
{ 
	Graphics::context->CopyResource(
		Graphics::copyForwardRenderBuffer.Get(), Graphics::basicMSBuffer.Get());

	Graphics::context->OMSetRenderTargets(1, Graphics::basicMSRTV.GetAddressOf(), nullptr);

	m_postEffect.FogFilter();
}

void App::RenderWaterFilter() 
{ 
	Graphics::context->OMSetRenderTargets(1, Graphics::basicRTV.GetAddressOf(), nullptr);

	m_postEffect.WaterFilter(); 
}

void App::RenderBloom() 
{ 
	m_postEffect.Bloom(Graphics::basicSRV, 3, Graphics::bloomRTV[0]);
	m_postEffect.CombineFromBloom(Graphics::basicSRV, Graphics::backBufferRTV);
}

void App::RenderFrustumCullingViewer() 
{
	Graphics::context->VSSetConstantBuffers(
		8, 1, m_camera.m_cullingViewerConstantBuffer.GetAddressOf());
	Graphics::context->RSSetViewports(1, &Graphics::cullingViewerViewport);

	float clearColor[4] = { 0.5f, 0.5f, 0.5f, 0.0f };
	Graphics::context->ClearRenderTargetView(Graphics::cullingViewerRTV.Get(), clearColor);
	Graphics::context->ClearDepthStencilView(
		Graphics::cullingViewerDSV.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	
	Graphics::context->OMSetRenderTargets(
		1, Graphics::cullingViewerRTV.GetAddressOf(), Graphics::cullingViewerDSV.Get());

	// chunk render
	{
		std::vector<ID3D11ShaderResourceView*> ppSRVs;
		ppSRVs.push_back(Graphics::blockAtlasMapSRV.Get());
		ppSRVs.push_back(Graphics::normalAtlasMapSRV.Get());
		ppSRVs.push_back(Graphics::merAtlasMapSRV.Get());
		ppSRVs.push_back(Graphics::grassColorMapSRV.Get());
		ppSRVs.push_back(Graphics::foliageColorMapSRV.Get());
		ppSRVs.push_back(Graphics::climateMapSRV.Get());
		Graphics::context->PSSetShaderResources(0, (UINT)ppSRVs.size(), ppSRVs.data());

		ChunkManager::GetInstance()->RenderBasicAlbedo();
	}
	
	// view frustum grid render
	{
		m_camera.RenderViewFrustum();
	}

	// To BackBuffer
	{
		Graphics::context->OMSetRenderTargets(1, Graphics::backBufferRTV.GetAddressOf(), nullptr);

		Graphics::context->PSSetShaderResources(0, 1, Graphics::cullingViewerSRV.GetAddressOf());

		Graphics::SetPipelineStates(Graphics::samplingGammaPSO);
		SimpleQuadRenderer::GetInstance()->Render();
	}

	Graphics::context->VSSetConstantBuffers(8, 1, m_camera.m_constantBuffer.GetAddressOf());
	Graphics::context->RSSetViewports(1, &Graphics::basicViewport);
}

void App::RenderReflectionWorld() 
{ 
	Graphics::SetPipelineStates(Graphics::samplingGammaPSO);
	Graphics::context->PSSetShaderResources(0, 1, Graphics::mirrorWorldSRV.GetAddressOf());

	Graphics::context->RSSetViewports(1, &Graphics::reflectionWorldViewport);
	SimpleQuadRenderer::GetInstance()->Render();

	Graphics::context->RSSetViewports(1, &Graphics::basicViewport);
}

void App::RenderGBufferViewer() 
{ 
	Graphics::SetPipelineStates(Graphics::samplingMSGammaPSO); 
	Graphics::context->PSSetShaderResources(0, 1, Graphics::albedoSRV.GetAddressOf());
	Graphics::context->RSSetViewports(1, &Graphics::GBufferViewerViewport[0]);
	SimpleQuadRenderer::GetInstance()->Render();

	Graphics::SetPipelineStates(Graphics::samplingMSPSO);
	Graphics::context->PSSetShaderResources(0, 1, Graphics::normalEdgeSRV.GetAddressOf());
	Graphics::context->RSSetViewports(1, &Graphics::GBufferViewerViewport[1]);
	SimpleQuadRenderer::GetInstance()->Render();

	Graphics::context->PSSetShaderResources(0, 1, Graphics::positionSRV.GetAddressOf());
	Graphics::context->RSSetViewports(1, &Graphics::GBufferViewerViewport[2]);
	SimpleQuadRenderer::GetInstance()->Render();

	Graphics::context->PSSetShaderResources(0, 1, Graphics::merSRV.GetAddressOf());
	Graphics::context->RSSetViewports(1, &Graphics::GBufferViewerViewport[3]);
	SimpleQuadRenderer::GetInstance()->Render();

	Graphics::SetPipelineStates(Graphics::samplingCoveragePSO);
	Graphics::context->PSSetShaderResources(0, 1, Graphics::coverageSRV.GetAddressOf());
	Graphics::context->RSSetViewports(1, &Graphics::GBufferViewerViewport[4]);
	SimpleQuadRenderer::GetInstance()->Render();
	
	Graphics::context->RSSetViewports(1, &Graphics::basicViewport);
}

void App::RenderSSAOViewer() 
{ 
	Graphics::SetPipelineStates(Graphics::samplingPSO);

	Graphics::context->PSSetShaderResources(0, 1, Graphics::ssaoSRV.GetAddressOf());

	SimpleQuadRenderer::GetInstance()->Render();
}

void App::LockCursor()
{
	if (!m_isActive) {
		POINT topLeft = { 0, 0 };
		POINT bottomRight = { APP_WIDTH, APP_HEIGHT };
		ClientToScreen(m_hwnd, &topLeft);	  // 좌측상단 포인터
		ClientToScreen(m_hwnd, &bottomRight); // 우측하단 포인터
		RECT clipRect = { topLeft.x, topLeft.y, bottomRight.x, bottomRight.y };

		ClipCursor(&clipRect);

		ShowCursor(false); // 전역 카운트로 동작하기 때문에 m_isActive를 이용한 flag 처리 필요

		m_isActive = true;
	}
}

void App::UnlockCursor()
{
	if (m_isActive) {
		ClipCursor(nullptr);

		ShowCursor(true);

		m_isActive = false;
	}
}