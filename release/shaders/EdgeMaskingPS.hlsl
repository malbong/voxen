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
    float minDiff = 987654321.0f;
    
    [unroll]
    for (uint i = 0; i < SAMPLE_COUNT; ++i)
    {
        invaildPos += positionTex.Load(input.posProj.xy, i).w;
        
        uint edge = normalEdgeTex.Load(input.posProj.xy, i).w; // edge 1, no edge 0, invaild -1
        sumEdge += (edge > 0) ? 1 : 0;
    }
    if (!sumEdge) // coverage가 아니면 엣지가 아님
        discard;
    if (invaildPos == -1.0 * SAMPLE_COUNT) // 유효하지 않은 위치가 SAMPLE 개수만큼 있으면 엣지가 아님
        discard;
    
    uint rough = 0;
    uint far = 0;
    float3 baseNormal = normalEdgeTex.Load(input.posProj.xy, 0).xyz;
    float3 basePosition = positionTex.Load(input.posProj.xy, 0).xyz;
    [unroll]
    for (uint j = 1; j < SAMPLE_COUNT; ++j)
    {
        float angle = dot(baseNormal, normalEdgeTex.Load(input.posProj.xy, j).xyz);
        rough += (angle < 0.98) ? 1 : 0;
        
        float dist = length(basePosition - positionTex.Load(input.posProj.xy, j).xyz);
        far += (dist > 1.0) ? 1 : 0;
    }
    
    if (!rough && !far) // 노멀 모두 비슷하고, 위치도 모두 비슷하면 엣지가 아님
        discard;
    
    // coverage가 다르고, 유효한 위치이며, 노멀이 다르거나 위치가 다른 경우인데
    // 카메라와의 거리가 최소 샘플 중, 최솟값이 임계값보다 멀면 엣지가 아님
    
    return float4(1, 0, 0, 0);
}