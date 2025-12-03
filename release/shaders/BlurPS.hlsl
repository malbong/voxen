#include "Common.hlsli"

Texture2D renderTex : register(t0);

struct psInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

static const float gaussianKernel[5] = { 0.0545, 0.2442, 0.4026, 0.2442, 0.0545 };

float4 main(psInput input) : SV_TARGET
{
    float4 color = float4(0.0, 0.0, 0.0, 0.0);
    float2 offset = float2(0.0, 0.0);
    
    float width, height, lod;
    renderTex.GetDimensions(0, width, height, lod);
  
    float dx = 1.0 / width;
    float dy = 1.0 / height;
    
    [unroll]
    for (int i = 0; i < 5; ++i)
    {
#ifdef BLUR_X
        offset = float2(dx * (i - 2), 0.0);
#endif
#ifdef BLUR_Y
        offset = float2(0.0, dy * (i - 2));
#endif
        float4 sampleColor = renderTex.Sample(linearClampSS, input.texcoord + offset);
        
#ifdef USE_ALPHA_BLUR
        color.rgb += sampleColor.rgb * sampleColor.a * gaussianKernel[i];
        color.a += sampleColor.a * gaussianKernel[i];
#else
        color += sampleColor * gaussianKernel[i];
#endif
    }
    
#ifdef USE_ALPHA_BLUR
    if (color.a > 0.0)
        color.rgb /= color.a;
#endif
    return color;
}