#include "Common.hlsli"

Texture2DArray shadowMap : register(t0);

struct psInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

float4 main(psInput input) : SV_TARGET
{
    float scaledX = input.texcoord.x * 3.0;
    uint cascadeIndex = floor(scaledX);
    float x = scaledX - cascadeIndex;
    
    float2 scaledTexcoord = float2(scaledX, input.texcoord.y);
    float depth = shadowMap.SampleLevel(linearWrapSS, float3(scaledTexcoord, cascadeIndex), 0.0).r;
    
    return float4(depth, 0, 0, 1);
}