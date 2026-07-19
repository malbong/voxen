#include "Common.hlsli"

cbuffer WaterFilterConstantBuffer : register(b0)
{
    float3 filterColor;
    float filterStrength;
}

struct psInput
{
    float4 posProj : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

float4 main(psInput input) : SV_TARGET
{
    float clampedRadianceWeight = clamp(radianceWeight, 0.1, 1.0);
    
    float blendAlpha = filterStrength;
    
    return float4(filterColor * clampedRadianceWeight, blendAlpha);
}