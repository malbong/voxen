#include "Common.hlsli"

Texture2DMS<float4, SAMPLE_COUNT> normalEdgeTex : register(t0);
Texture2DMS<float4, SAMPLE_COUNT> positionTex : register(t1);

struct psInput
{
    float4 posProj : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

float4 main(psInput input) : SV_Target
{
    float sumEdge = 0;
    float invaildPos = 0;
    
    [unroll]
    for (uint i = 0; i < SAMPLE_COUNT; ++i)
    {
        invaildPos += positionTex.Load(input.posProj.xy, i).w;
        
        float4 sample = normalEdgeTex.Load(input.posProj.xy, i);
        
        uint edge = sample.w; // edge 1, no edge 0, invaild -1
        sumEdge += (edge > 0) ? 1 : 0;
    }
    if (!sumEdge)
        discard;
    if (invaildPos == -1.0 * SAMPLE_COUNT)
        discard;
    
    bool flat = true;
    bool close = true;
    float3 baseNormal = normalEdgeTex.Load(input.posProj.xy, 0).xyz;
    float3 basePosition = positionTex.Load(input.posProj.xy, 0).xyz;
    [unroll]
    for (uint j = 1; j < SAMPLE_COUNT; ++j)
    {
        float angle = dot(baseNormal, normalEdgeTex.Load(input.posProj.xy, j).xyz);
        if (angle < 0.98)
            flat = false;
        
        float dist = length(basePosition - positionTex.Load(input.posProj.xy, j).xyz);
        if (dist > 1.0)
            close = false;
    }
    if (flat && close)
        discard;
    
    return float4(1, 0, 0, 0);
}