#include "Light.h"
#include "DXUtils.h"
#include "MeshGenerator.h"
#include "Date.h"

#include <algorithm>

using namespace DirectX::SimpleMath;

Light::Light()
	: m_dir(0.0, 0.0f, 1.0f), m_scale(0.0f), m_radianceColor(1.0f), m_radianceWeight(1.0f),
	  m_lightConstantBuffer(nullptr), m_shadowConstantBuffer(nullptr)
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
		Matrix rotationAxisMatrix = Matrix::CreateFromAxisAngle(Vector3(-1.0f, 0.0f, 0.0f), angle);

		m_dir = Vector3::Transform(Vector3(0.0, 0.0f, 1.0f), rotationAxisMatrix);
		m_dir.Normalize();

		m_up = XMVector3TransformNormal(Vector3(0.0f, 1.0f, 0.0f), rotationAxisMatrix);

		// shadow dir
		float shadowAngle = angle;
		shadowAngle /= (2.0f * Utils::PI) / 2880.0f;
		shadowAngle = std::floor(shadowAngle);
		shadowAngle *= (2.0f * Utils::PI) / 2880.0f;
		Matrix shadowRotationAxisMatrix =
			Matrix::CreateFromAxisAngle(Vector3(-1.0f, 0.0f, 0.0f), shadowAngle);
		m_shadowDir = Vector3::Transform(Vector3(0.0, 0.0f, 1.0f), shadowRotationAxisMatrix);
		m_shadowDir.Normalize();
		m_shadowUp = XMVector3TransformNormal(Vector3(0.0f, 1.0f, 0.0f), shadowRotationAxisMatrix);

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
		else if (Date::NIGHT_END <= dateTime &&
				 dateTime <= Date::DAY_START + Date::DAY_CYCLE_AMOUNT) {

			if (Date::NIGHT_END <= dateTime && dateTime < Date::MAX_SUNRISE) {
				float radianceColorFactor =
					(float)(dateTime - Date::NIGHT_END) / (Date::MAX_SUNRISE - Date::NIGHT_END);

				m_radianceColor =
					Utils::Lerp(RADIANCE_NIGHT_COLOR, RADIANCE_SUNRISE_COLOR, radianceColorFactor);
			}

			if (Date::MAX_SUNRISE <= dateTime &&
				dateTime < Date::DAY_START + Date::DAY_CYCLE_AMOUNT) {
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
		m_shadowConstantData.width = CASCADE_SIZE;
		m_shadowConstantData.height = CASCADE_SIZE;
		m_shadowConstantData.cascadeLevel = CASCADE_LEVEL;

		Matrix cameraViewProjInverse =
			(camera.GetViewMatrix() * camera.GetProjectionMatrix()).Invert();
		Matrix lightShadowViewMatrix = GetShadowViewMatrix();
		Matrix lightShadowViewInvMatrix = lightShadowViewMatrix.Invert();

		Vector3 frustumPoints[8] = { { -1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f },
			{ 1.0f, -1.0f, 0.0f }, { -1.0f, -1.0f, 0.0f }, { -1.0f, 1.0f, 1.0f },
			{ 1.0f, 1.0f, 1.0f }, { 1.0f, -1.0f, 1.0f }, { -1.0f, -1.0f, 1.0f } };


		/*
		 * cascade
		 * - camera viewfrustum을 적절한 비율로 자를 것
		 * - 꼭 들어갈 frustum 비율임
		 * constantDiagonals
		 * - 잘린 subfrustum을 포함하는 넉넉한 길이를 정적으로 정의
		 * - subfrustum의 near-far의 대각선이나, far평면의 대각선 성분중 큰 값을 정적으로 정의
		 * - shadow가 렌더링할 실질적인 크기가 될 것
		 */
		const float cascadeScale[CASCADE_LEVEL + 1] = { 0.00f, 0.01f, 0.03f, 0.08f };
		const float constantDiagonals[CASCADE_LEVEL] = { 36.0f, 110.0f, 275.0f };

		/*
		* NDC frustum points -> world frustum points
		* - NDC * (VP)-1 => World Frustum Points 
		*/
		Vector3 worldFrustumPoints[8];
		for (int i = 0; i < 8; ++i) {
			worldFrustumPoints[i] = Vector3::Transform(frustumPoints[i], cameraViewProjInverse);
		}


		float cascadeFarDist[CASCADE_LEVEL] = { 0.0f, };
		/*
		* cascade 마다 구별되는 constant Buffer 구성
		* - projection Matrix를 구성하기 위한 절차
		*/
		for (int cascadeIndex = 0; cascadeIndex < CASCADE_LEVEL; ++cascadeIndex) {

			Vector3 cascadeWorldFrustumPoints[8];
			Vector3 cascadeLightViewFrustumPoints[8];

			/*
			* light view world frustum을 cascade에 맞는 sub frustum으로 잘라 light view로 변환
			*/
			for (int j = 0; j < 4; ++j) {
				Vector3 worldNearToFar = worldFrustumPoints[j + 4] - worldFrustumPoints[j];

				Vector3 toNear = worldNearToFar * cascadeScale[cascadeIndex];
				Vector3 toFar = worldNearToFar * cascadeScale[cascadeIndex + 1];

				cascadeWorldFrustumPoints[j] = worldFrustumPoints[j] + toNear;
				cascadeWorldFrustumPoints[j + 4] = worldFrustumPoints[j] + toFar;

				cascadeLightViewFrustumPoints[j] =
					Vector3::Transform(cascadeWorldFrustumPoints[j], lightShadowViewMatrix);
				cascadeLightViewFrustumPoints[j + 4] =
					Vector3::Transform(cascadeWorldFrustumPoints[j + 4], lightShadowViewMatrix);
			}


			/*
			* light view 에서의 최대, 최소 벡터를 만듦
			* - 이는 `XMMatrixOrthographicOffCenterLH()`에서 필요한 인자들임
			* - 위의 함수는 center를 기준으로 min/max.xyz 를 필요로 함
			*/
			Vector3 lightViewPointsMin = Vector3(D3D11_FLOAT32_MAX);
			Vector3 lightViewPointsMax = Vector3(-D3D11_FLOAT32_MAX);
			for (int j = 0; j < 8; ++j) {
				lightViewPointsMin = XMVectorMin(lightViewPointsMin, cascadeLightViewFrustumPoints[j]);
				lightViewPointsMax = XMVectorMax(lightViewPointsMax, cascadeLightViewFrustumPoints[j]);
			}


			/*
			* 단순히 lightViewPointsMin을 이용하여 projection matrix를 구성할 경우 문제가 존재
			* 1. 너무 딱 맞는 크기만 쉐도우 맵으로 렌더링됨 - 그림자가 생기지 않을 경우도 존재
			* 2. 약간의 카메라 흔들림으로 값이 변화하여 그림자가 흔들리는 문제가 생길 수 있음
			* - 이를 제거하기 위해 Diagonal 이라는 전체를 감싸고도 충분한 크기를 설정함
			* - 또한 Diagonal이 결국 정투영될 전체 크기이므로 이를 활용하여 world size를 texel에 snap함
			*/
			Vector3 vDiagonal(constantDiagonals[cascadeIndex]);
			Vector3 maxminVector = lightViewPointsMax - lightViewPointsMin;
			Vector3 borderOffset = (vDiagonal - maxminVector) * 0.5f;

			/*
			* Shadow Map으로 렌더링할 전체 크기를 vDiagonal 크기로 맞춰주는 과정
			* borderOffset: (전체길이 - 실제길이) / 2
			* - 차이의 절반을 큰쪽에 더하고 작은쪽에 뺌으로 써 전체 크기가 vDiagonal이 됨
			* - MS SDK Sample에서는 borderOffset.z = 0 으로 값을 꺼버리고 연산함 -> 이후에 near/far 따로 연산
			* - 해당 프로젝트는 z에 대해 정적인 값을 두고 사용하기 위해 z를 그냥 두고 사용
			*/
			lightViewPointsMax += borderOffset;
			lightViewPointsMin -= borderOffset;


			/*
			* Stable Shadow Map을 위한 world size snap
			* - 하나의 텍셀당 world 크기를 구함
			* - 이는 vDiagonal 값이 정적이기 때문에 가능함
			*/
			float worldUnitsPerTexel = vDiagonal.x / (float)CASCADE_SIZE;


			/*
			* 실질적인 snap 과정
			* - 임의의 변수 X를 나누고(/) 소수점을 버리고(std::floor) 다시 X를 곱하는 과정은 양자화임
			* - [X, 2X)의 값들이 X로 양자화됨
			* 
			* X가 텍셀당 world size
			* - min, max의 움직임이 텍셀당 world size 만큼 움직여야 이동될 것
			* - 예를 들면, 하나의 텍셀에 숟가락이 들어갔다면, 그 숟가락은 텍셀 한칸에 무조건 고정되게 됨
			* - 이렇게 snap하게 되면 임의의 텍셀 사이에 숟가락이 들어가지 않아 카메라의 움직임에 안정적이게 됨
			* 
			* z 값도 단순히 snap
			* - 카메라의 움직임에 연관있는 데이터는 min/max.xy임
			* - 흔들림과 크게 관련이 없는 데이터 z 이지만, 일관성을 위해 유지
			* - 추가로 z값은 vDiagonal에 의해 near->far distance가 정적일 것
			*/
			float minX = std::floor(lightViewPointsMin.x / worldUnitsPerTexel) * worldUnitsPerTexel;
			float minY = std::floor(lightViewPointsMin.y / worldUnitsPerTexel) * worldUnitsPerTexel;
			float minZ = std::floor(lightViewPointsMin.z / worldUnitsPerTexel) * worldUnitsPerTexel;
			lightViewPointsMin = Vector3(minX, minY, minZ);

			float maxX = std::floor(lightViewPointsMax.x / worldUnitsPerTexel) * worldUnitsPerTexel;
			float maxY = std::floor(lightViewPointsMax.y / worldUnitsPerTexel) * worldUnitsPerTexel;
			float maxZ = std::floor(lightViewPointsMax.z / worldUnitsPerTexel) * worldUnitsPerTexel;
			lightViewPointsMax = Vector3(maxX, maxY, maxZ);

			
			/*
			*  projection matrix 구성 후 constant data에 넣음
			*/
			m_proj[cascadeIndex] = XMMatrixOrthographicOffCenterLH(lightViewPointsMin.x, lightViewPointsMax.x,
				lightViewPointsMin.y, lightViewPointsMax.y, lightViewPointsMin.z,
				lightViewPointsMax.z);

			m_shadowConstantData.viewProj[cascadeIndex] = (lightShadowViewMatrix * m_proj[cascadeIndex]).Transpose();

			/*
			 * 카메라 거리에 따라 Cascade Blending을 하기 위한 splits 데이터 설정
			 * - 실제로 카메라를 감싸는 Light View Frustum의 크기가 커짐
			 * - 그것을 world로 변환하여 카메라와의 z 거리를 계산하고 최대치를 splits 값으로 설정
			 */
			Vector3 lightViewPoints[8] = {
				{ minX, minY, minZ },
				{ maxX, minY, minZ },
				{ minX, maxY, minZ },
				{ maxX, maxY, minZ },
				{ minX, minY, maxZ },
				{ maxX, minY, maxZ },
				{ minX, maxY, maxZ },
				{ maxX, maxY, maxZ },
			};

			Vector3 cameraPos = camera.GetPosition();
			Vector3 cameraDir = camera.GetForward();
			float maxDist = -D3D11_FLOAT32_MAX;

			for (int i = 0; i < 8; ++i) {
				Vector3 lightWorldPoint =
					Vector3::Transform(lightViewPoints[i], lightShadowViewInvMatrix);
				float worldDistanceZ = (lightWorldPoint - cameraPos).Dot(cameraDir);
				maxDist = max(worldDistanceZ, maxDist);
			}

			cascadeFarDist[cascadeIndex] = maxDist;
		} // end for cascade index

		float farZ = camera.GetFarZ();
		m_shadowConstantData.cascadeSplits =
			Vector4(0.0, farZ * cascadeScale[1], farZ * cascadeScale[2], farZ * cascadeScale[3]);
	
		DXUtils::UpdateConstantBuffer(m_shadowConstantBuffer, m_shadowConstantData);
	}
}