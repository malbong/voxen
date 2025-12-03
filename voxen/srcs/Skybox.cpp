#include "Date.h"
#include "Skybox.h"
#include "Graphics.h"
#include "DXUtils.h"
#include "MeshGenerator.h"

#include <algorithm>

Skybox::Skybox()
	: m_stride(sizeof(SkyboxVertex)), m_offset(0), m_vertexBuffer(nullptr), m_indexBuffer(nullptr)
{
}

Skybox::~Skybox() {}

bool Skybox::Initialize(float scale)
{
	MeshGenerator::CreateSkyboxMesh(m_vertices, m_indices, scale);
	std::reverse(m_indices.begin(), m_indices.end()); // for front CW at inner

	if (!DXUtils::CreateVertexBuffer(m_vertexBuffer, m_vertices)) {
		std::cout << "failed create vertex buffer in skybox" << std::endl;
		return false;
	}

	if (!DXUtils::CreateIndexBuffer(m_indexBuffer, m_indices)) {
		std::cout << "failed create index buffer in skybox" << std::endl;
		return false;
	}

	m_constantData.skyScale = scale;
	if (!DXUtils::CreateConstantBuffer(m_constantBuffer, m_constantData)) {
		std::cout << "failed create constant buffer in skybox" << std::endl;
		return false;
	}

	return true;
}

void Skybox::Update(uint32_t dateTime)
{
	// set color & strength
	Vector3 normalHorizonColor = Vector3(0.0f);
	Vector3 normalZenithColor = Vector3(0.0f);
	Vector3 sunHorizonColor = Vector3(0.0f);
	Vector3 sunZenithColor = Vector3(0.0f);

	if (Date::DAY_START <= dateTime && dateTime < Date::DAY_END) { // day
		normalHorizonColor = NORMAL_DAY_HORIZON;
		normalZenithColor = NORMAL_DAY_ZENITH;
		sunHorizonColor = SUN_DAY_HORIZON;
		sunZenithColor = SUN_DAY_ZENITH;
	}
	else if (Date::NIGHT_START <= dateTime && dateTime < Date::NIGHT_END) { // night
		normalHorizonColor = NORMAL_NIGHT_HORIZON;
		normalZenithColor = NORMAL_NIGHT_ZENITH;
		sunHorizonColor = NORMAL_NIGHT_HORIZON;
		sunZenithColor = NORMAL_NIGHT_ZENITH;
	}
	else { // mix
		if (dateTime < Date::DAY_START)
			dateTime += Date::DAY_CYCLE_AMOUNT;

		// normal color
		if (Date::DAY_END <= dateTime && dateTime < Date::NIGHT_START) {
			float w = (float)(dateTime - Date::DAY_END) / (Date::NIGHT_START - Date::DAY_END);

			normalHorizonColor = Utils::Lerp(NORMAL_DAY_HORIZON, NORMAL_NIGHT_HORIZON, w);
			normalZenithColor = Utils::Lerp(NORMAL_DAY_ZENITH, NORMAL_NIGHT_ZENITH, w);
		} // 11000 ~ 13700 | 22300 ~ 25000
		else if (Date::NIGHT_END <= dateTime && dateTime <= Date::DAY_START + Date::DAY_CYCLE_AMOUNT) {
			float w = (float)(dateTime - Date::NIGHT_END) /
					  (Date::DAY_START + Date::DAY_CYCLE_AMOUNT - Date::NIGHT_END);

			normalHorizonColor = Utils::Lerp(NORMAL_NIGHT_HORIZON, NORMAL_DAY_HORIZON, w);
			normalZenithColor = Utils::Lerp(NORMAL_NIGHT_ZENITH, NORMAL_DAY_ZENITH, w);
		}

		// sun color
		if (Date::DAY_END <= dateTime && dateTime < Date::MAX_SUNSET) { // day ~ sunset
			float w = (float)(dateTime - Date::DAY_END) / (Date::MAX_SUNSET - Date::DAY_END);

			sunHorizonColor = Utils::Lerp(SUN_DAY_HORIZON, SUN_SUNSET_HORIZON, w);
			sunZenithColor = Utils::Lerp(SUN_DAY_ZENITH, SUN_SUNSET_ZENITH, w);
		}
		else if (Date::MAX_SUNSET <= dateTime && dateTime < Date::NIGHT_START) { // sunset ~ night
			float w = (float)(dateTime - Date::MAX_SUNSET) / (Date::NIGHT_START - Date::MAX_SUNSET);

			sunHorizonColor = Utils::Lerp(SUN_SUNSET_HORIZON, NORMAL_NIGHT_HORIZON, w);
			sunZenithColor = Utils::Lerp(SUN_SUNSET_ZENITH, NORMAL_NIGHT_ZENITH, w);
		}
		else if (Date::NIGHT_END <= dateTime && dateTime < Date::MAX_SUNRISE) { // night ~ sunrise
			float w = (float)(dateTime - Date::NIGHT_END) / (Date::MAX_SUNRISE - Date::NIGHT_END);

			sunHorizonColor = Utils::Lerp(NORMAL_NIGHT_HORIZON, SUN_SUNRISE_HORIZON, w);
			sunZenithColor = Utils::Lerp(NORMAL_NIGHT_ZENITH, SUN_SUNRISE_ZENITH, w);
		}
		else { // sunrise ~ day
			float w = (float)(dateTime - Date::MAX_SUNRISE) /
					  (Date::DAY_START + Date::DAY_CYCLE_AMOUNT - Date::MAX_SUNRISE);

			sunHorizonColor = Utils::Lerp(SUN_SUNRISE_HORIZON, SUN_DAY_HORIZON, w);
			sunZenithColor = Utils::Lerp(SUN_SUNRISE_ZENITH, SUN_DAY_ZENITH, w);
		}
	}

	m_constantData.normalHorizonColor = Utils::SRGB2Linear(normalHorizonColor);
	m_constantData.normalZenithColor = Utils::SRGB2Linear(normalZenithColor);
	m_constantData.sunHorizonColor = Utils::SRGB2Linear(sunHorizonColor);
	m_constantData.sunZenithColor = Utils::SRGB2Linear(sunZenithColor);

	DXUtils::UpdateConstantBuffer(m_constantBuffer, m_constantData);
}

void Skybox::Render()
{
	Graphics::context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	Graphics::context->IASetVertexBuffers(
		0, 1, m_vertexBuffer.GetAddressOf(), &m_stride, &m_offset);

	std::vector<ID3D11ShaderResourceView*> ppSRVs;
	ppSRVs.push_back(Graphics::sunSRV.Get());
	ppSRVs.push_back(Graphics::moonSRV.Get());
	Graphics::context->PSSetShaderResources(0, (UINT)ppSRVs.size(), ppSRVs.data());

	Graphics::context->DrawIndexed((UINT)m_indices.size(), 0, 0);
}