#include "Common.hlsli"

cbuffer PickingBlockConstantBuffer : register(b0)
{
    matrix world;
}

struct vsInput
{
    float3 position : POSITION;
    float3 color : COLOR;
};

struct vsOutput
{
    float4 posProj : SV_POSITION;
    float3 color : COLOR;
};

vsOutput main(vsInput input)
{
    vsOutput output;
    
    output.posProj = float4(input.position, 1.0);
    
    output.posProj = mul(output.posProj, world);
    output.posProj = mul(output.posProj, view);
    output.posProj = mul(output.posProj, proj);
    
    output.color = input.color;
    
	return output;
}