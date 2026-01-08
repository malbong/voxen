#pragma once

#include <d3d11.h>
#include <vector>
#include <wrl.h>

#include "Structure.h"

using namespace Microsoft::WRL;

class PostEffect {
public:
	PostEffect();
	~PostEffect();

	bool Initialize();
	void Update(float dt, bool isUnderWater);

	void Blur(int count, ComPtr<ID3D11ShaderResourceView>& src, ComPtr<ID3D11RenderTargetView>& dst,
		ComPtr<ID3D11ShaderResourceView> blurSRV[2], ComPtr<ID3D11RenderTargetView> blurRTV[2],
		ComPtr<ID3D11PixelShader> blurPS[2]);
	void Bloom();

	ComPtr<ID3D11Buffer> m_fogFilterConstantBuffer;
	ComPtr<ID3D11Buffer> m_waterFilterConstantBuffer;
	ComPtr<ID3D11Buffer> m_ssaoConstantBuffer;
	ComPtr<ID3D11Buffer> m_ssaoNoiseConstantBuffer;

private:
	FogFilterConstantData m_fogFilterConstantData;
	WaterFilterConstantData m_waterFilterConstantData;
	SsaoConstantData m_ssaoConstantData;
	SsaoNoiseConstantData m_ssaoNoiseConstantData;

	float m_waterAdaptationTime;
	float m_waterMaxDuration;
};