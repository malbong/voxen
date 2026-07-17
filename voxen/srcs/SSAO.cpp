#include "SSAO.h"
#include "DXUtils.h"
#include "SimpleQuadRenderer.h"

#include <random>

SSAO::SSAO() : m_constantBuffer(nullptr), m_noiseConstantBuffer(nullptr)
{
}

SSAO::~SSAO() {}

bool SSAO::Initialize()
{
	const UINT KERNEL_COUNT = 16;

	std::uniform_real_distribution<float> randomFloats(0.0001f, 1.0f);
	std::default_random_engine generator;
	for (UINT i = 0; i < KERNEL_COUNT; ++i) {
		Vector4 sampleKernel;

		sampleKernel.x = randomFloats(generator) * 2.0f - 1.0f;
		sampleKernel.y = randomFloats(generator) * 2.0f - 1.0f;
		sampleKernel.z = randomFloats(generator); // hemisphere
		sampleKernel.w = 0.0f;
		sampleKernel.Normalize();

		sampleKernel *= randomFloats(generator); // Random Scaling

		float scale = (float)i / KERNEL_COUNT;
		sampleKernel *= Utils::Lerp(0.1f, 1.0f, scale * scale); // Á¡ÁøÀû Scaling

		m_constantData.sampleKernel[i] = sampleKernel;
	}
	if (!DXUtils::CreateConstantBuffer(m_constantBuffer, m_constantData)) {
		std::cout << "failed create ssao constant buffer" << std::endl;
		return false;
	}

	for (UINT i = 0; i < KERNEL_COUNT; ++i) {
		Vector4 rotationNoise;

		rotationNoise.x = randomFloats(generator) * 2.0f - 1.0f;
		rotationNoise.y = randomFloats(generator) * 2.0f - 1.0f;
		rotationNoise.z = randomFloats(generator) * 2.0f - 1.0f;
		rotationNoise.w = 0.0f;
		rotationNoise.Normalize();

		m_noiseConstantData.rotationNoise[i] = rotationNoise;
	}
	if (!DXUtils::CreateConstantBuffer(m_noiseConstantBuffer, m_noiseConstantData)) {
		std::cout << "failed create ssao noise constant buffer" << std::endl;
		return false;
	}

	return true;
}

void SSAO::Render()
{
	std::vector<ID3D11Buffer*> ppConstants;
	ppConstants.push_back(m_constantBuffer.Get());
	ppConstants.push_back(m_noiseConstantBuffer.Get());
	Graphics::context->PSSetConstantBuffers(0, (UINT)ppConstants.size(), ppConstants.data());

	std::vector<ID3D11ShaderResourceView*> ppSRVs;
	ppSRVs.push_back(Graphics::normalEdgeSRV.Get());
	ppSRVs.push_back(Graphics::positionSRV.Get());
	ppSRVs.push_back(Graphics::coverageSRV.Get());
	Graphics::context->PSSetShaderResources(0, (UINT)ppSRVs.size(), ppSRVs.data());

	Graphics::SetPipelineStates(Graphics::ssaoPSO);
	SimpleQuadRenderer::GetInstance()->Render();

	Graphics::SetPipelineStates(Graphics::ssaoEdgePSO);
	SimpleQuadRenderer::GetInstance()->Render();
}