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
    
    float4 sumColor = float4(0.0, 0.0, 0.0, 0.0);
    float sumWeight = 0.0;

    float4 base = renderTex.Sample(linearClampSS, input.texcoord);
    float sigma = 0.325;

    sumColor  += base;
    sumWeight += 1.0;

    [unroll]
    for (int i = -1; i <= 1; ++i)
    {
        for (int j = -1; j <= 1; ++j)
        {
            if (i == 0 && j == 0)
                continue;

            float2 offset = float2(dx * i, dy * j);
            float4 s = renderTex.Sample(linearClampSS, input.texcoord + offset);

            float diff = length(base - s);
            float w = exp(-diff * diff / (sigma * sigma)) * pow(s, 1.25);

            sumColor  += s * w;
            sumWeight += w;
        }
    }

    return sumColor / sumWeight;
}