#include "Common.hlsli"

Texture2DArray blockAtlasTextureArray : register(t0);

struct psInput
{
    float4 posProj : SV_POSITION;
    float2 texcoord : TEXCOORD;
    uint texIndex : INDEX;
    uint VPIndex : SV_ViewportArrayIndex;
};

float4 main(psInput input) : SV_TARGET
{
    if (blockAtlasTextureArray.SampleLevel(pointWrapSS, float3(input.texcoord, input.texIndex), 0.0).a != 1.0)
        discard;
    
    return float4(1, 1, 1, 1);
}