#pragma once

#include <d3d11.h>
#include <wrl.h>
#include <vector>

using namespace Microsoft::WRL;

class GraphicsPSO
{
public:
	GraphicsPSO();
	GraphicsPSO(const GraphicsPSO& rhs);
	GraphicsPSO operator=(const GraphicsPSO& other);
	~GraphicsPSO();
	
	ComPtr<ID3D11InputLayout> inputLayout;
	D3D11_PRIMITIVE_TOPOLOGY topology;

	ComPtr<ID3D11VertexShader> vertexShader;

	ComPtr<ID3D11GeometryShader> geometryShader;

	ComPtr<ID3D11RasterizerState> rasterizerState;

	ComPtr<ID3D11PixelShader> pixelShader;
	
	std::vector<ID3D11SamplerState *> samplerStates;

	ComPtr<ID3D11DepthStencilState> depthStencilState;
	UINT stencilRef;


	ComPtr<ID3D11BlendState> blendState;
	float blendFactor[4];
	
	/*
	ComPtr<ID3D11HullShader> m_hullShader;
    ComPtr<ID3D11DomainShader> m_domainShader;
    
	UINT m_stencilRef = 0;
	*/
};