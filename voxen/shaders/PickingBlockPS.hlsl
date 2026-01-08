#include "Common.hlsli"

struct psInput
{
    float4 posProj : SV_POSITION;
    float3 color : COLOR;
};

float4 main(psInput input) : SV_TARGET
{
    return float4(input.color, 1.0);
}