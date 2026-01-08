#include "Common.hlsli"

Texture2D samplingTexture : register(t0);

struct psInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

float4 main(psInput input) : SV_TARGET
{
    return samplingTexture.SampleLevel(linearWrapSS, input.texcoord, 0.0);
}