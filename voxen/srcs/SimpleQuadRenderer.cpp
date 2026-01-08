#include "SimpleQuadRenderer.h"
#include "MeshGenerator.h"
#include "DXUtils.h"

SimpleQuadRenderer* SimpleQuadRenderer::simpleQuadRenderer = nullptr;

SimpleQuadRenderer* SimpleQuadRenderer::GetInstance()
{
	if (simpleQuadRenderer == nullptr) {
		simpleQuadRenderer = new SimpleQuadRenderer();

		if (!simpleQuadRenderer->Initialize()) {
			std::cout << "Simple Quad Renderer initialize failed" << std::endl;
			return nullptr;
		}
	}

	return simpleQuadRenderer;
}

SimpleQuadRenderer::SimpleQuadRenderer()
	: m_stride(sizeof(SamplingVertex)), m_offset(0), m_vertexBuffer(nullptr), m_indexBuffer(nullptr)
{
}

SimpleQuadRenderer::~SimpleQuadRenderer() {}

bool SimpleQuadRenderer::Initialize()
{
	MeshGenerator::CreateSampleSquareMesh(m_vertices, m_indices);

	if (!DXUtils::CreateVertexBuffer(m_vertexBuffer, m_vertices)) {
		std::cout << "failed create sampling vertex buffer" << std::endl;
		return false;
	}

	if (!DXUtils::CreateIndexBuffer(m_indexBuffer, m_indices)) {
		std::cout << "failed create sampling index buffer" << std::endl;
		return false;
	}

	return true;
}

void SimpleQuadRenderer::Render()
{
	Graphics::context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	Graphics::context->IASetVertexBuffers(
		0, 1, m_vertexBuffer.GetAddressOf(), &m_stride, &m_offset);

	Graphics::context->DrawIndexed((UINT)m_indices.size(), 0, 0);
}