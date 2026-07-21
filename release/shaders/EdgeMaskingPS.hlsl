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
    if (useFullSemiAlphaEdge)
    {
        return float4(1, 0, 0, 1);
    }
    // 1. 마킹 값 체크
    const float INVALID_MASK = -1.0;
    const float EDGE_MASK = 1.0;
    const float SEMIALPHA_MASK = 2.0;
    
    uint invalidPosition = 0;
    uint edgeCount = 0;
    uint semiAlphaCount = 0;
    
    [unroll]
    for (uint i = 0; i < SAMPLE_COUNT; ++i)
    {
        float ne_w = normalEdgeTex.Load(input.posProj.xy, i).w; // invaild -1, noEdge 0, edge 1, semialpha 2
        
        if (ne_w == INVALID_MASK)
            ++invalidPosition;
        else if (ne_w == EDGE_MASK)
            ++edgeCount;
        else if (ne_w == SEMIALPHA_MASK)
            ++semiAlphaCount;
    }
    
    if (invalidPosition == SAMPLE_COUNT) // 유효하지 않은 위치가 SAMPLE 개수만큼 있으면 엣지가 아님
        discard;
    
    if (useFullSemiAlphaEdge == true)
    {
        if (edgeCount == 0 && semiAlphaCount == 0)
            discard;
    }
    else
    {
        bool isSemiAlphaEdgePixel = (0 < semiAlphaCount && semiAlphaCount < SAMPLE_COUNT);
        if (edgeCount == 0 && !isSemiAlphaEdgePixel)
            discard;
    }
    
    
    // 2. rough, far 엣지 체크
    uint rough = 0;
    uint far = 0;
    float3 baseNormal = normalEdgeTex.Load(input.posProj.xy, 0).xyz;
    float3 basePosition = positionTex.Load(input.posProj.xy, 0).xyz;
    const float ROUGH_THRESHOLD = 0.98;
    const float DISTANCE_THRESHOLD = 1.0;
    
    [unroll]
    for (uint j = 1; j < SAMPLE_COUNT; ++j)
    {
        float angle = dot(baseNormal, normalEdgeTex.Load(input.posProj.xy, j).xyz);
        rough += (angle < ROUGH_THRESHOLD) ? 1 : 0;
        
        float dist = length(basePosition - positionTex.Load(input.posProj.xy, j).xyz);
        far += (dist > DISTANCE_THRESHOLD) ? 1 : 0;
    }
    
    if (!rough && !far) // 노멀 모두 비슷하고, 위치도 모두 비슷하면 엣지가 아님
        discard;
    
    return float4(1, 0, 0, 0);
}