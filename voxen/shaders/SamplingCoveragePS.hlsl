#include "Common.hlsli"

Texture2DMS<uint, SAMPLE_COUNT> samplingTextureMS : register(t0);

struct psInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

float4 main(psInput input) : SV_TARGET
{
    uint texWidth, texHeight, sampleCount;
    samplingTextureMS.GetDimensions(texWidth, texHeight, sampleCount);
    int2 texCoord = int2(input.texcoord * float2(texWidth, texHeight));
    
    bool edge = false;
    [unroll]
    for (int i = 0; i < SAMPLE_COUNT; ++i)
    {
        uint coverage = samplingTextureMS.Load(texCoord, i);
        if (coverage != 0xF)
        {
            edge = true;
        }
    }
    
    float3 color = edge ? float3(1, 0, 0) : float3(0, 0, 0);
    return float4(color, 1.0);
}