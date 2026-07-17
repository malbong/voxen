#include "Common.hlsli"

struct vsInput
{
    float3 posModel : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD;
    matrix instanceWorld : WORLD;
    uint texIndex : INDEX;
};

struct vsOutput
{
#ifdef USE_SHADOW
    float4 posProj : SV_POSITION;
    float2 texcoord : TEXCOORD;
    uint texIndex : INDEX;
#else
    float4 posProj : SV_POSITION;
    sample float3 posWorld : POSITION;
    sample float3 normal : NORMAL;
    sample float2 texcoord : TEXCOORD;
    uint texIndex : INDEX;
#endif
};

vsOutput main(vsInput input)
{
    vsOutput output;
    
    output.posProj = mul(float4(input.posModel, 1.0), input.instanceWorld);
    
#ifndef USE_SHADOW
    output.posWorld = output.posProj.xyz;
    
    output.posProj = mul(output.posProj, view);
    output.posProj = mul(output.posProj, proj);
    
    output.normal = mul(float4(input.normal, 0.0), input.instanceWorld).xyz; // invTranspose 고려하지 않음 -> ununiform scaling X
    float3 toEye = normalize(eyePos - output.posWorld);
    if (dot(output.normal, toEye) < 0.0)
        output.normal *= -1;
#endif
    
    output.texcoord = input.texcoord;
    
    output.texIndex = input.texIndex;
    
    return output;
}