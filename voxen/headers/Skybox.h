#pragma once

#include <d3d11.h>
#include <vector>
#include <wrl.h>

#include "Structure.h"
#include "Utils.h"

using namespace Microsoft::WRL;

class Skybox {
public:
	Skybox();
	~Skybox();

	bool Initialize(float scale);
	void Update(uint32_t dateTime);
	void Render();

	ComPtr<ID3D11Buffer> m_constantBuffer;

private:
	// normal color
	const Vector3 NORMAL_DAY_HORIZON = Vector3(0.67f, 0.82f, 1.0f);
	const Vector3 NORMAL_DAY_ZENITH = Vector3(0.52f, 0.67f, 1.0f);

	const Vector3 NORMAL_NIGHT_HORIZON = Vector3(0.19f, 0.20f, 0.24f);
	const Vector3 NORMAL_NIGHT_ZENITH = Vector3(0.17f, 0.175f, 0.195f);

	// sun color
	const Vector3 SUN_DAY_HORIZON = Vector3(0.60f, 0.74f, 1.0f);
	const Vector3 SUN_DAY_ZENITH = Vector3(0.32f, 0.45f, 1.0f);

	const Vector3 SUN_SUNRISE_HORIZON = Vector3(0.72f, 0.60f, 0.34f);
	const Vector3 SUN_SUNRISE_ZENITH =
		Utils::Lerp(SUN_DAY_ZENITH, NORMAL_NIGHT_ZENITH, 15.0f / 27.0f);

	const Vector3 SUN_SUNSET_HORIZON = Vector3(0.64f, 0.26f, 0.04f);
	const Vector3 SUN_SUNSET_ZENITH =
		Utils::Lerp(SUN_DAY_ZENITH, NORMAL_NIGHT_ZENITH, 15.0f / 27.0f);

	SkyboxConstantData m_constantData;

	std::vector<SkyboxVertex> m_vertices;
	std::vector<uint32_t> m_indices;
	UINT m_stride;
	UINT m_offset;
	ComPtr<ID3D11Buffer> m_vertexBuffer;
	ComPtr<ID3D11Buffer> m_indexBuffer;
};