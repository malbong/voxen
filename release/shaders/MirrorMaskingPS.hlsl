#include "Common.hlsli"

Texture2DMS<float4, SAMPLE_COUNT> worldDepth : register(t0);

struct psInput
{
    float4 posProj : SV_POSITION;
    float3 posWorld : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD;
    uint texIndex : INDEX;
};

float main(psInput input) : SV_Target0
{
    if (input.normal.y <= 0 || input.posWorld.y < 64.0 - 1e-4 || 64.0 + 1e-4 < input.posWorld.y)
        discard;
    
    float pixelDepth = input.posProj.z;
        
    int2 base = int2(input.posProj.xy - 0.5) * 2;                                                                                                                                                                                                                                                
    float minDepth = 1.0;
    [unroll]                                                                                                                                                                                                                                                                                                             
    for (uint s = 0; s < SAMPLE_COUNT; ++s)
    {
        minDepth = min(minDepth, worldDepth.Load(base, s).r);
        minDepth = min(minDepth, worldDepth.Load(base + int2(1, 0), s).r);
        minDepth = min(minDepth, worldDepth.Load(base + int2(0, 1), s).r);
        minDepth = min(minDepth, worldDepth.Load(base + int2(1, 1), s).r);
    }

    if (minDepth + 1e-4 <= pixelDepth)
        discard;
    
    return pixelDepth;
}