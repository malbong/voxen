#pragma once

#include <d3d11.h>
#include <wrl.h>

#include "Structure.h"

using namespace Microsoft::WRL;

class SSAO
{
public:
	SSAO();
	~SSAO();

	bool Initialize();
	void Render();


private:
	ComPtr<ID3D11Buffer> m_constantBuffer;
	ComPtr<ID3D11Buffer> m_noiseConstantBuffer;

	SsaoConstantData m_constantData;
	SsaoNoiseConstantData m_noiseConstantData;
};