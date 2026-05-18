#include "Common.hlsli"

Texture2D renderTex : register(t0);

struct psInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

float4 main(psInput input) : SV_TARGET
{   
    float width, height, lod;
    renderTex.GetDimensions(0, width, height, lod);
    
    float dx = 1.0 / width;
    float dy = 1.0 / height;
    
    float sumColor = float4(0.0, 0.0, 0.0, 0.0);
    float sumWeight = 0;
    
    float base = renderTex.Sample(linearClampSS, input.texcoord).r;
    float sigma = 0.2;
    
    [unroll]
    for (int i = -1; i <= 1; ++i)
    {
        for (int j = -1; j <= 1; ++j)
        {
            float2 offset = float2(dx * i, dy * j);
            float s = renderTex.Sample(linearClampSS, input.texcoord + offset).r;
        
            float diff = abs(base - s);
            float w = exp(-diff * diff / (2.0 * sigma * sigma)); // (1/e) ^ (diff^2 / 2 * sigma^2) 
           
            sumColor += s * w;
            sumWeight += w;
        }
    }
        
    return sumColor / sumWeight;
}