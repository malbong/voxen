#include "Common.hlsli"

struct vsInput
{
    float3 position : POSITION;
};

struct vsOutput
{
    float4 posProj : SV_POSITION;
    float3 posWorld : POSITION;
};

vsOutput main(vsInput input)
{
    vsOutput output;
    
    output.posWorld = input.position;
    
    output.posProj.xyz = output.posWorld;
    //output.posProj.y += 550.0 * sin(3.14159265 / 24.0);
    
    output.posProj = mul(float4(output.posProj.xyz, 0.0), view);
    output.posProj = mul(float4(output.posProj.xyz, 1.0), proj);
    
    return output;
}