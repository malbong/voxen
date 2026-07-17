#include "Common.hlsli"

struct vsInput
{
    float3 position : POSITION;
};

struct vsOutput
{
    float4 posProj : SV_POSITION;
    float3 color : COLOR;
};

vsOutput main(vsInput input)
{
    vsOutput output;
    
    float4 frustumNDCPosition = float4(input.position, 1.0);
    
    float4 frustumViewPosition = mul(frustumNDCPosition, invProj);
    frustumViewPosition.xyz /= frustumViewPosition.w;
    
    float4 frustumWorldPosition = mul(frustumViewPosition, invView);
    
    output.posProj = mul(frustumWorldPosition, view);
    output.posProj = mul(output.posProj, proj);
    
    output.color = float3(1.0, 0.0, 0.0);
    
    return output;
}