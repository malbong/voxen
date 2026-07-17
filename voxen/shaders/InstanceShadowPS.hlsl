#include "Common.hlsli"

Texture2DArray blockAtlasTextureArray : register(t0);

struct psInput
{
    float4 posProj : SV_POSITION;
    float2 texcoord : TEXCOORD;
    uint texIndex : INDEX;
    uint RTIndex : SV_RenderTargetArrayIndex;
};

float4 main(psInput input) : SV_TARGET
{
    const uint LAST_CASCADE_INDEX = 2;

    if (input.RTIndex == LAST_CASCADE_INDEX)
    {
        discard;
    }
    
    if (blockAtlasTextureArray.SampleLevel(pointWrapSS, float3(input.texcoord, input.texIndex), 0.0).a < 1.0)
        discard;
    
    return float4(1, 1, 1, 1);
}