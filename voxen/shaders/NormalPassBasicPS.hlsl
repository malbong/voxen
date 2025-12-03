#include "Common.hlsli"

Texture2DArray atlasTextureArray : register(t0);

struct vsOutput
{
    float4 posProj : SV_POSITION;
    float3 posWorld : POSITION1;
    sample float3 posModel : POSITION2;
    uint face : FACE;
    uint type : TYPE;
};

float4 main(vsOutput input) : SV_TARGET
{
    float2 texcoord = getVoxelTexcoord(input.posModel, input.face);
    uint index = (input.type - 1) * 6 + input.face;
    
    if (atlasTextureArray.SampleLevel(pointWrapSS, float3(texcoord, index), 0.0).a != 1.0)
        discard;

    // must be [Normal * ITWorld * ITView]
    float3 normalWorld = getNormal(input.face);
    float3 normalView = mul(float4(normalWorld, 0.0), view).xyz;
    return float4(normalView, 0.0);
}