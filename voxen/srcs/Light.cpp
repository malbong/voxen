#include "Light.h"
#include "DXUtils.h"
#include "MeshGenerator.h"
#include "Date.h"

#include <algorithm>

using namespace DirectX::SimpleMath;

Light::Light()
	: m_dir(cos(Utils::PI / 4.0f), 0.0f, cos(Utils::PI / 4.0f)), m_scale(0.0f),
	  m_radianceColor(1.0f), m_radianceWeight(1.0f), m_lightConstantBuffer(nullptr),
	  m_shadowConstantBuffer(nullptr)
{
}

Light::~Light() {}

bool Light::Initialize()
{
	if (!DXUtils::CreateConstantBuffer(m_lightConstantBuffer, m_lightConstantData)) {
		std::cout << "failed create constant buffer in light" << std::endl;
		return false;
	}

	if (!DXUtils::CreateConstantBuffer(m_shadowConstantBuffer, m_shadowConstantData)) {
		std::cout << "failed create constant buffer in shadow" << std::endl;
		return false;
	}

	return true;
}

void Light::Update(UINT dateTime, Camera& camera)
{
	const float MAX_RADIANCE_WEIGHT = 2.0;
	float angle = (float)dateTime / Date::DAY_CYCLE_AMOUNT * 2.0f * Utils::PI;

	// light
	{
		// dir
		Matrix rotationAxisMatrix = Matrix::CreateFromAxisAngle(
			Vector3(-cos(Utils::PI / 4.0f), 0.0f, cos(Utils::PI / 4.0f)), angle);

		m_dir = Vector3::Transform(
			Vector3(cos(Utils::PI / 4.0f), 0.0f, cos(Utils::PI / 4.0f)), rotationAxisMatrix);
		m_dir.Normalize();

		m_up = XMVector3TransformNormal(Vector3(0.0f, 1.0f, 0.0f), rotationAxisMatrix);

		// strength
		if (Date::NIGHT_START <= dateTime && dateTime < Date::NIGHT_END) {
			m_radianceWeight = 0.0f;
		}
		else {
			float currentAltitude = m_dir.y;
			float nightEndAltitude = -1.7f / 12.0f * Utils::PI;
			
			float w = (currentAltitude - nightEndAltitude) / (1.0f - nightEndAltitude);

			m_radianceWeight = Utils::Smootherstep(0.0f, MAX_RADIANCE_WEIGHT, w);
		}

		// color
		if (dateTime < Date::DAY_START)
			dateTime += Date::DAY_CYCLE_AMOUNT;

		if (Date::DAY_START <= dateTime && dateTime < Date::DAY_END) {
			m_radianceColor = RADIANCE_DAY_COLOR;
		}
		else if (Date::DAY_END <= dateTime && dateTime < Date::NIGHT_START) {
			if (Date::DAY_END <= dateTime && dateTime < Date::MAX_SUNSET) {
				float radianceColorFactor =
					(float)(dateTime - Date::DAY_END) / (Date::MAX_SUNSET - Date::DAY_END);

				m_radianceColor =
					Utils::Lerp(RADIANCE_DAY_COLOR, RADIANCE_SUNSET_COLOR, radianceColorFactor);
			}

			if (Date::MAX_SUNSET <= dateTime && dateTime < Date::NIGHT_START) {
				float radianceColorFactor =
					(float)(dateTime - Date::MAX_SUNSET) / (Date::NIGHT_START - Date::MAX_SUNSET);

				m_radianceColor =
					Utils::Lerp(RADIANCE_SUNSET_COLOR, RADIANCE_NIGHT_COLOR, radianceColorFactor);
			}
		}
		else if (Date::NIGHT_START <= dateTime && dateTime < Date::NIGHT_END) {
			m_radianceColor = RADIANCE_NIGHT_COLOR;
		}
		else if (Date::NIGHT_END <= dateTime && dateTime <= Date::DAY_START + Date::DAY_CYCLE_AMOUNT) {

			if (Date::NIGHT_END <= dateTime && dateTime < Date::MAX_SUNRISE) {
				float radianceColorFactor =
					(float)(dateTime - Date::NIGHT_END) / (Date::MAX_SUNRISE - Date::NIGHT_END);

				m_radianceColor =
					Utils::Lerp(RADIANCE_NIGHT_COLOR, RADIANCE_SUNRISE_COLOR, radianceColorFactor);
			}

			if (Date::MAX_SUNRISE <= dateTime && dateTime < Date::DAY_START + Date::DAY_CYCLE_AMOUNT) {
				float radianceColorFactor =
					(float)(dateTime - Date::MAX_SUNRISE) /
					(Date::DAY_START + Date::DAY_CYCLE_AMOUNT - Date::MAX_SUNRISE);

				m_radianceColor =
					Utils::Lerp(RADIANCE_SUNRISE_COLOR, RADIANCE_DAY_COLOR, radianceColorFactor);
			}
		}

		m_lightConstantData.lightDir = m_dir;
		m_lightConstantData.radianceWeight = m_radianceWeight;
		m_lightConstantData.radianceColor = Utils::SRGB2Linear(m_radianceColor);
		m_lightConstantData.maxRadianceWeight = MAX_RADIANCE_WEIGHT;

		DXUtils::UpdateConstantBuffer(m_lightConstantBuffer, m_lightConstantData);
	}


	// shadow
	{
		float cascade[CASCADE_NUM + 1] = { 0.0f, 0.015f, 0.035f, 0.15f};
		float diagonals[CASCADE_NUM] = { 30.0f, 88.0f, 337.0f };

		Matrix cameraViewProjInverse =
			(camera.GetViewMatrix() * camera.GetProjectionMatrix()).Invert();
		Matrix lightViewMatrix = GetViewMatrix();

		Vector3 frustumCorner[8]{ { -1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f },
			{ 1.0f, -1.0f, 0.0f }, { -1.0f, -1.0f, 0.0f },

			{ -1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, -1.0f, 1.0f },
			{ -1.0f, -1.0f, 1.0f } };

		// camera proj -> world space
		Vector3 worldFrustumCorner[8];
		for (int i = 0; i < 8; ++i) {
			worldFrustumCorner[i] = Vector3::Transform(frustumCorner[i], cameraViewProjInverse);
		}

		for (int i = 0; i < CASCADE_NUM; ++i) {
			// slice frustum to fit cascade
			Vector3 worldCascadeCorner[8];
			Vector3 lightViewCascadeCorner[8];
			for (int j = 0; j < 4; ++j) {
				Vector3 worldBeginToEnd = worldFrustumCorner[j + 4] - worldFrustumCorner[j];

				Vector3 toNear = worldBeginToEnd * cascade[i];
				Vector3 toFar = worldBeginToEnd * cascade[i + 1];

				worldCascadeCorner[j] = worldFrustumCorner[j] + toNear;
				worldCascadeCorner[j + 4] = worldFrustumCorner[j] + toFar;

				lightViewCascadeCorner[j] =
					Vector3::Transform(worldCascadeCorner[j], lightViewMatrix);
				lightViewCascadeCorner[j + 4] =
					Vector3::Transform(worldCascadeCorner[j + 4], lightViewMatrix);
			}

			Vector3 lightViewCornerMin = Vector3(D3D11_FLOAT32_MAX);
			Vector3 lightViewCornerMax = Vector3(-D3D11_FLOAT32_MAX);
			for (int j = 0; j < 8; ++j) {
				lightViewCornerMin = XMVectorMin(lightViewCornerMin, lightViewCascadeCorner[j]);
				lightViewCornerMax = XMVectorMax(lightViewCornerMax, lightViewCascadeCorner[j]);
			}
			
			Vector3 diagonal(diagonals[i]);
			float cascadeBound = diagonals[i];

			Vector3 maxminVector = lightViewCornerMax - lightViewCornerMin;
			Vector3 borderOffset = (diagonal - maxminVector) * 0.5f;

			lightViewCornerMax += borderOffset;
			lightViewCornerMin -= borderOffset;
			
			float worldUnitsPerTexel = cascadeBound / (float)CASCADE_SIZE;

			lightViewCornerMin /= worldUnitsPerTexel;
			lightViewCornerMin = XMVectorFloor(lightViewCornerMin);
			lightViewCornerMin *= worldUnitsPerTexel;

			lightViewCornerMax /= worldUnitsPerTexel;
			lightViewCornerMax = XMVectorFloor(lightViewCornerMax);
			lightViewCornerMax *= worldUnitsPerTexel;

			m_proj[i] = XMMatrixOrthographicOffCenterLH(lightViewCornerMin.x, lightViewCornerMax.x,
				lightViewCornerMin.y, lightViewCornerMax.y, lightViewCornerMin.z,
				lightViewCornerMax.z);

			m_shadowConstantData.viewProj[i] = (lightViewMatrix * m_proj[i]).Transpose();
			m_shadowConstantData.topLX[i] = (float)CASCADE_SIZE * i;
			m_shadowConstantData.viewportWidth[i] = (float)CASCADE_SIZE;
		}

		DXUtils::UpdateConstantBuffer(m_shadowConstantBuffer, m_shadowConstantData);
	}
}