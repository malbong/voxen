#include "Common.hlsli"

cbuffer CloudConstantBuffer : register(b0)
{
    matrix world;
    float3 volumeColor;
    float cloudScale;
}

struct vsInput
{
    float3 position : POSITION;
    uint face : FACE;
};

struct vsOutput
{
    float4 posProj : SV_POSITION;
    float3 posWorld : POSITION;
    uint face : FACE;
};

vsOutput main(vsInput input)
{
    vsOutput output;
    
    output.face = input.face;
    
    output.posWorld = mul(float4(input.position, 1.0), world).xyz;
    
    output.posProj = mul(float4(output.posWorld, 1.0), view);
    output.posProj = mul(output.posProj, proj);
    
    return output;
}