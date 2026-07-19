#include "Common.hlsli"
#include "Lighting.hlsli"

struct psInput
{
    float4 posProj : SV_POSITION;
    float3 posWorld : POSITION;
};

float4 main(psInput input) : SV_TARGET
{
    float3 posDir = normalize(input.posWorld);
#ifdef USE_MIRROR
    posDir.y *= -1;
#endif
    
    return float4(getSkyColor(posDir), 1.0);
}


