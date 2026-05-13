#pragma once

#include <d3d11.h>
#include <wrl.h>

#include "GraphicsPSO.h"
#include "Light.h"

using namespace Microsoft::WRL;

namespace Graphics {
	// Graphics Core
	extern ComPtr<ID3D11Device> device;
	extern ComPtr<ID3D11DeviceContext> context;
	extern ComPtr<IDXGISwapChain> swapChain;


	// Input Layout
	extern ComPtr<ID3D11InputLayout> basicIL;
	extern ComPtr<ID3D11InputLayout> skyboxIL;
	extern ComPtr<ID3D11InputLayout> cloudIL;
	extern ComPtr<ID3D11InputLayout> samplingIL;
	extern ComPtr<ID3D11InputLayout> instanceIL;
	extern ComPtr<ID3D11InputLayout> pickingBlockIL;
	extern ComPtr<ID3D11InputLayout> viewFrustumIL;


	// Vertex Shader
	extern ComPtr<ID3D11VertexShader> basicVS;
	extern ComPtr<ID3D11VertexShader> basicAlphaClipVS;
	extern ComPtr<ID3D11VertexShader> skyboxVS;
	extern ComPtr<ID3D11VertexShader> cloudVS;
	extern ComPtr<ID3D11VertexShader> samplingVS;
	extern ComPtr<ID3D11VertexShader> instanceVS;
	extern ComPtr<ID3D11VertexShader> basicShadowVS;
	extern ComPtr<ID3D11VertexShader> instanceShadowVS;
	extern ComPtr<ID3D11VertexShader> pickingBlockVS;
	extern ComPtr<ID3D11VertexShader> viewFrustumVS;


	// Geometry Shader
	extern ComPtr<ID3D11GeometryShader> basicShadowGS;
	extern ComPtr<ID3D11GeometryShader> instanceShadowGS;


	// Pixel Shader
	extern ComPtr<ID3D11PixelShader> basicPS;
	extern ComPtr<ID3D11PixelShader> basicAlphaClipPS;
	extern ComPtr<ID3D11PixelShader> basicMirrorPS;
	extern ComPtr<ID3D11PixelShader> basicMirrorAlphaClipPS;
	extern ComPtr<ID3D11PixelShader> basicAlbedoPS;
	extern ComPtr<ID3D11PixelShader> skyboxPS;
	extern ComPtr<ID3D11PixelShader> skyboxMirrorPS;
	extern ComPtr<ID3D11PixelShader> cloudPS;
	extern ComPtr<ID3D11PixelShader> samplingPS;
	extern ComPtr<ID3D11PixelShader> fogFilterPS;
	extern ComPtr<ID3D11PixelShader> mirrorMaskingPS;
	extern ComPtr<ID3D11PixelShader> waterPlanePS;
	extern ComPtr<ID3D11PixelShader> waterFilterPS;
	extern ComPtr<ID3D11PixelShader> blurMirrorPS[2];
	extern ComPtr<ID3D11PixelShader> blurSsaoPS[2];
	extern ComPtr<ID3D11PixelShader> ssaoPS;
	extern ComPtr<ID3D11PixelShader> ssaoEdgePS;
	extern ComPtr<ID3D11PixelShader> edgeMaskingPS;
	extern ComPtr<ID3D11PixelShader> shadingBasicPS;
	extern ComPtr<ID3D11PixelShader> shadingBasicEdgePS;
	extern ComPtr<ID3D11PixelShader> bloomDownPS;
	extern ComPtr<ID3D11PixelShader> bloomUpPS;
	extern ComPtr<ID3D11PixelShader> combineBloomPS;
	extern ComPtr<ID3D11PixelShader> instanceShadowPS;
	extern ComPtr<ID3D11PixelShader> biomeMapPS;
	extern ComPtr<ID3D11PixelShader> pickingBlockPS;


	// Rasterizer State
	extern ComPtr<ID3D11RasterizerState> solidRS;
	extern ComPtr<ID3D11RasterizerState> wireRS;
	extern ComPtr<ID3D11RasterizerState> noneCullRS;
	extern ComPtr<ID3D11RasterizerState> mirrorRS;
	extern ComPtr<ID3D11RasterizerState> shadowRS;
	extern ComPtr<ID3D11RasterizerState> noneCullDepthBiasRS;
	extern ComPtr<ID3D11RasterizerState> noneDepthClipRS;

	// Sampler State
	extern ComPtr<ID3D11SamplerState> pointWrapSS;
	extern ComPtr<ID3D11SamplerState> linearWrapSS;
	extern ComPtr<ID3D11SamplerState> pointClampSS;
	extern ComPtr<ID3D11SamplerState> linearClampSS;
	extern ComPtr<ID3D11SamplerState> shadowCompareSS;
	

	// Depth Stencil State
	extern ComPtr<ID3D11DepthStencilState> basicDSS;
	extern ComPtr<ID3D11DepthStencilState> stencilMaskDSS;
	extern ComPtr<ID3D11DepthStencilState> stencilEqualDrawDSS;
	extern ComPtr<ID3D11DepthStencilState> mirrorDrawMaskedDSS;

	
	// Blend State
	extern ComPtr<ID3D11BlendState> alphaBS;


	// Render Target Buffer
	extern ComPtr<ID3D11Texture2D> backBuffer;
	extern ComPtr<ID3D11RenderTargetView> backBufferRTV;

	extern ComPtr<ID3D11Texture2D> basicBuffer;
	extern ComPtr<ID3D11RenderTargetView> basicRTV;
	extern ComPtr<ID3D11ShaderResourceView> basicSRV;

	extern ComPtr<ID3D11Texture2D> basicMSBuffer;
	extern ComPtr<ID3D11RenderTargetView> basicMSRTV;
	extern ComPtr<ID3D11ShaderResourceView> basicMSSRV;

	extern ComPtr<ID3D11Texture2D> normalEdgeBuffer;
	extern ComPtr<ID3D11RenderTargetView> normalEdgeRTV;
	extern ComPtr<ID3D11ShaderResourceView> normalEdgeSRV;

	extern ComPtr<ID3D11Texture2D> positionBuffer;
	extern ComPtr<ID3D11RenderTargetView> positionRTV;
	extern ComPtr<ID3D11ShaderResourceView> positionSRV;

	extern ComPtr<ID3D11Texture2D> albedoBuffer;
	extern ComPtr<ID3D11RenderTargetView> albedoRTV;
	extern ComPtr<ID3D11ShaderResourceView> albedoSRV;

	extern ComPtr<ID3D11Texture2D> coverageBuffer;
	extern ComPtr<ID3D11RenderTargetView> coverageRTV;
	extern ComPtr<ID3D11ShaderResourceView> coverageSRV;

	extern ComPtr<ID3D11Texture2D> merBuffer;
	extern ComPtr<ID3D11RenderTargetView> merRTV;
	extern ComPtr<ID3D11ShaderResourceView> merSRV;

	extern ComPtr<ID3D11Texture2D> ssaoBuffer;
	extern ComPtr<ID3D11RenderTargetView> ssaoRTV;
	extern ComPtr<ID3D11ShaderResourceView> ssaoSRV;

	extern ComPtr<ID3D11Texture2D> ssaoBlurBuffer[2];
	extern ComPtr<ID3D11RenderTargetView> ssaoBlurRTV[2];
	extern ComPtr<ID3D11ShaderResourceView> ssaoBlurSRV[2];

	extern ComPtr<ID3D11Texture2D> mirrorWorldBuffer;
	extern ComPtr<ID3D11RenderTargetView> mirrorWorldRTV;
	extern ComPtr<ID3D11ShaderResourceView> mirrorWorldSRV;

	extern ComPtr<ID3D11Texture2D> mirrorDepthRenderBuffer;
	extern ComPtr<ID3D11RenderTargetView> mirrorDepthRTV;
	extern ComPtr<ID3D11ShaderResourceView> mirrorDepthSRV;

	extern ComPtr<ID3D11Texture2D> mirrorBlurBuffer[2];
	extern ComPtr<ID3D11RenderTargetView> mirrorBlurRTV[2];
	extern ComPtr<ID3D11ShaderResourceView> mirrorBlurSRV[2];

	extern ComPtr<ID3D11Texture2D> bloomBuffer[5];
	extern ComPtr<ID3D11RenderTargetView> bloomRTV[5];
	extern ComPtr<ID3D11ShaderResourceView> bloomSRV[5];

	extern ComPtr<ID3D11Texture2D> cullingViewerBuffer;
	extern ComPtr<ID3D11RenderTargetView> cullingViewerRTV;
	extern ComPtr<ID3D11ShaderResourceView> cullingViewerSRV;


	// Depth Stencil Buffer
	extern ComPtr<ID3D11Texture2D> basicDepthBuffer;
	extern ComPtr<ID3D11DepthStencilView> basicDSV;
	extern ComPtr<ID3D11ShaderResourceView> basicDepthSRV;

	extern ComPtr<ID3D11Texture2D> deferredDepthBuffer;
	extern ComPtr<ID3D11DepthStencilView> deferredDSV;

	extern ComPtr<ID3D11Texture2D> mirrorWorldDepthBuffer;
	extern ComPtr<ID3D11DepthStencilView> mirrorWorldDSV;

	extern ComPtr<ID3D11Texture2D> shadowBuffer;
	extern ComPtr<ID3D11DepthStencilView> shadowDSV;
	extern ComPtr<ID3D11ShaderResourceView> shadowSRV;

	extern ComPtr<ID3D11Texture2D> cullingViewerDepthBuffer;
	extern ComPtr<ID3D11DepthStencilView> cullingViewerDSV;


	// Shadow Resource Buffer
	extern ComPtr<ID3D11Texture2D> blockAtlasMapBuffer;
	extern ComPtr<ID3D11ShaderResourceView> blockAtlasMapSRV;

	extern ComPtr<ID3D11Texture2D> normalAtlasMapBuffer;
	extern ComPtr<ID3D11ShaderResourceView> normalAtlasMapSRV;

	extern ComPtr<ID3D11Texture2D> merAtlasMapBuffer;
	extern ComPtr<ID3D11ShaderResourceView> merAtlasMapSRV;

	extern ComPtr<ID3D11Texture2D> waterStillAtlasMapBuffer;
	extern ComPtr<ID3D11ShaderResourceView> waterStillAtlasMapSRV;

	extern ComPtr<ID3D11Texture2D> waterStillNormalAtlasMapBuffer;
	extern ComPtr<ID3D11ShaderResourceView> waterStillNormalAtlasMapSRV;

	extern ComPtr<ID3D11Texture2D> grassColorMapBuffer;
	extern ComPtr<ID3D11ShaderResourceView> grassColorMapSRV;

	extern ComPtr<ID3D11Texture2D> foliageColorMapBuffer;
	extern ComPtr<ID3D11ShaderResourceView> foliageColorMapSRV;

	extern ComPtr<ID3D11Texture2D> waterColorMapBuffer;
	extern ComPtr<ID3D11ShaderResourceView> waterColorMapSRV;

	extern ComPtr<ID3D11Texture2D> sunBuffer;
	extern ComPtr<ID3D11ShaderResourceView> sunSRV;

	extern ComPtr<ID3D11Texture2D> moonBuffer;
	extern ComPtr<ID3D11ShaderResourceView> moonSRV;

	extern ComPtr<ID3D11Texture2D> copyForwardRenderBuffer;
	extern ComPtr<ID3D11ShaderResourceView> copyForwardSRV;

	extern ComPtr<ID3D11Texture2D> biomeMapBuffer;
	extern ComPtr<ID3D11ShaderResourceView> biomeMapSRV;

	extern ComPtr<ID3D11Texture2D> climateMapBuffer;
	extern ComPtr<ID3D11ShaderResourceView> climateMapSRV;

	extern ComPtr<ID3D11Texture2D> worldPointBuffer;
	extern ComPtr<ID3D11ShaderResourceView> worldPointSRV;

	extern ComPtr<ID3D11Texture2D> brdfBuffer;
	extern ComPtr<ID3D11ShaderResourceView> brdfSRV;
		

	// Viewport
	extern D3D11_VIEWPORT basicViewport;
	extern D3D11_VIEWPORT mirrorWorldViewport;
	extern D3D11_VIEWPORT bloomViewport;
	extern D3D11_VIEWPORT worldMapViewport;
	extern D3D11_VIEWPORT shadowViewports[Light::CASCADE_NUM];
	extern D3D11_VIEWPORT cullingViewerViewport;
	extern D3D11_VIEWPORT reflectionWorldViewport;


	// device, context, swapChain
	extern bool InitGraphicsCore(DXGI_FORMAT pixelFormat, HWND& hwnd);
	

	// RTV, DSV, SRV (+ UAV ...)
	extern bool InitGraphicsBuffer();
	extern bool InitRenderTargetBuffers();
	extern bool InitDepthStencilBuffers();
	extern bool InitShaderResourceBuffers();
	

	// VS, IL, PS, RS, SS, DSS (+ HS, DS, GS, BS ...)
	extern bool InitGraphicsState();
	extern bool InitVertexShaderAndInputLayouts();
	extern bool InitGeometryShaders();
	extern bool InitPixelShaders();
	extern bool InitRasterizerStates();
	extern bool InitSamplerStates();
	extern bool InitDepthStencilStates();
	extern bool InitBlendStates();
	extern void InitViewports();


	// PSO
	extern void InitGraphicsPSO();
	extern void SetPipelineStates(GraphicsPSO& pso);
	extern GraphicsPSO basicPSO;
	extern GraphicsPSO basicMirrorPSO;
	extern GraphicsPSO basicAlbedoPSO;
	extern GraphicsPSO semiAlphaPSO;
	extern GraphicsPSO skyboxPSO;
	extern GraphicsPSO skyboxMirrorPSO;
	extern GraphicsPSO cloudPSO;
	extern GraphicsPSO cloudMirrorPSO;
	extern GraphicsPSO samplingPSO;
	extern GraphicsPSO fogFilterPSO;
	extern GraphicsPSO instancePSO;
	extern GraphicsPSO instanceMirrorPSO;
	extern GraphicsPSO mirrorMaskingPSO;
	extern GraphicsPSO waterPlanePSO;
	extern GraphicsPSO waterFilterPSO;
	extern GraphicsPSO basicDepthPSO;
	extern GraphicsPSO instanceDepthPSO;
	extern GraphicsPSO basicShadowPSO;
	extern GraphicsPSO instanceShadowPSO; 
	extern GraphicsPSO ssaoPSO;
	extern GraphicsPSO ssaoEdgePSO;
	extern GraphicsPSO edgeMaskingPSO;
	extern GraphicsPSO shadingBasicPSO;
	extern GraphicsPSO shadingBasicEdgePSO;
	extern GraphicsPSO bloomDownPSO;
	extern GraphicsPSO bloomUpPSO;
	extern GraphicsPSO combineBloomPSO;
	extern GraphicsPSO biomeMapPSO;
	extern GraphicsPSO pickingBlockPSO;
	extern GraphicsPSO viewFrustumPSO;
}
