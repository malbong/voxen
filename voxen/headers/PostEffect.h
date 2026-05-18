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

	void BlurGaussian(int count, ComPtr<ID3D11ShaderResourceView>& src,
		ComPtr<ID3D11RenderTargetView>& dst, ComPtr<ID3D11ShaderResourceView> blurSRV[2],
		ComPtr<ID3D11RenderTargetView> blurRTV[2]);
	void BlurBilateral(int count, ComPtr<ID3D11ShaderResourceView>& src,
		ComPtr<ID3D11RenderTargetView>& dst, ComPtr<ID3D11ShaderResourceView> blurSRV[2],
		ComPtr<ID3D11RenderTargetView> blurRTV[2]);
	void Bloom(
		ComPtr<ID3D11ShaderResourceView>& srv, int count, ComPtr<ID3D11RenderTargetView>& rtv);
	void CombineFromBloom(ComPtr<ID3D11ShaderResourceView>& originSRV, ComPtr<ID3D11RenderTargetView>& rtv);

	void FogFilter();
	void WaterFilter();

	ComPtr<ID3D11Buffer> m_fogFilterConstantBuffer;
	ComPtr<ID3D11Buffer> m_waterFilterConstantBuffer;
	

private:
	FogFilterConstantData m_fogFilterConstantData;
	WaterFilterConstantData m_waterFilterConstantData;
	
	float m_waterAdaptationTime;
	float m_waterMaxDuration;
};