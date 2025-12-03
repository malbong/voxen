#pragma once

#include <vector>
#include <d3d11.h>
#include <wrl.h>

#include "Structure.h"

using namespace Microsoft::WRL;

class SimpleQuadRenderer
{
public:
	static SimpleQuadRenderer* GetInstance();

	SimpleQuadRenderer();
	~SimpleQuadRenderer();

	bool Initialize();
	void Render();

private:
	static SimpleQuadRenderer* simpleQuadRenderer;

	std::vector<SamplingVertex> m_vertices;
	std::vector<uint32_t> m_indices;

	UINT m_stride;
	UINT m_offset;

	ComPtr<ID3D11Buffer> m_vertexBuffer;
	ComPtr<ID3D11Buffer> m_indexBuffer;
};