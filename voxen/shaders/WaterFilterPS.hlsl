#include "Common.hlsli"

Texture2D renderTex : register(t0);

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
    float3 renderColor = renderTex.Sample(linearClampSS, input.texcoord).rgb;
   
    float3 blendColor = lerp(renderColor, filterColor * clamp(radianceWeight, 0.1, 1.0), filterStrength);
    
    return float4(blendColor, 1.0);
}