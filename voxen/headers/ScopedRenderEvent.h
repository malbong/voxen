#pragma once

#include <wrl.h>
#include <d3d11.h>

#include "Graphics.h"

using namespace Microsoft::WRL;

class ScopedRenderEvent {
public:
	ScopedRenderEvent(const wchar_t* eventName)
	{
		Graphics::context->QueryInterface(IID_PPV_ARGS(&m_annotation));
		m_annotation->BeginEvent(eventName);
	}
	~ScopedRenderEvent() { m_annotation->EndEvent(); }

private:
	ComPtr<ID3DUserDefinedAnnotation> m_annotation;
};

#define CONCAT_IMPL(a, b) a##b
#define CONCAT(a, b) CONCAT_IMPL(a, b)
#define SCOPED_RENDER_EVENT(name) ScopedRenderEvent CONCAT(_renderEvent_, __COUNTER__)(L##name)
