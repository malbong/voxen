#include "GraphicsPSO.h"

GraphicsPSO::GraphicsPSO()
	: inputLayout(nullptr), topology(D3D_PRIMITIVE_TOPOLOGY_UNDEFINED), vertexShader(nullptr),
	  geometryShader(nullptr), rasterizerState(nullptr), pixelShader(nullptr), samplerStates(),
	  depthStencilState(nullptr), stencilRef(0),
	  blendState(nullptr), blendFactor{ 1.0f, 1.0f, 1.0f, 1.0f }
{
}

GraphicsPSO::GraphicsPSO(const GraphicsPSO& rhs)
	: inputLayout(rhs.inputLayout), topology(rhs.topology), vertexShader(rhs.vertexShader),
	  geometryShader(rhs.geometryShader), rasterizerState(rhs.rasterizerState),
	  pixelShader(rhs.pixelShader), samplerStates(rhs.samplerStates),
	  depthStencilState(rhs.depthStencilState), stencilRef(rhs.stencilRef),
	  blendState(rhs.blendState)
{
	blendFactor[0] = rhs.blendFactor[0];
	blendFactor[1] = rhs.blendFactor[1];
	blendFactor[2] = rhs.blendFactor[2];
	blendFactor[3] = rhs.blendFactor[3];
}

GraphicsPSO GraphicsPSO::operator=(const GraphicsPSO& other)
{
	inputLayout = other.inputLayout;
	topology = other.topology;

	vertexShader = other.vertexShader;

	geometryShader = other.geometryShader;

	rasterizerState = other.rasterizerState;

	pixelShader = other.pixelShader;

	samplerStates = other.samplerStates;

	depthStencilState = other.depthStencilState;
	stencilRef = other.stencilRef;

	blendState = other.blendState;
	blendFactor[0] = other.blendFactor[0];
	blendFactor[1] = other.blendFactor[1];
	blendFactor[2] = other.blendFactor[2];
	blendFactor[3] = other.blendFactor[3];

	return *this;
}

GraphicsPSO::~GraphicsPSO() {}
