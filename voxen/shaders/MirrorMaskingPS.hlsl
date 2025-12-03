#include "Common.hlsli"

Texture2DMS<float4, SAMPLE_COUNT> positionTex : register(t0);

struct psInput
{
    float4 posProj : SV_POSITION;
    float3 posWorld : POSITION;
    float3 normal : NORMAL;
    sample float2 texcoord : TEXCOORD;
    uint texIndex : INDEX;
};

float main(psInput input) : SV_Target0
{
    if (input.normal.y <= 0 || input.posWorld.y < 64.0 - 1e-4 || 64.0 + 1e-4 < input.posWorld.y)
        discard;
    
    float pixelDepth = input.posProj.z;
    
    float2 texcoord = float2(input.posProj.x / mirrorWidth, input.posProj.y / mirrorHeight);
    float2 appScreenCoord = float2(texcoord.x * appWidth, texcoord.y * appHeight);
    
    float4 position = positionTex.Load(appScreenCoord, 0);
    if (position.w == -1.0)
        position.z = 1000.0f;
    
    float4 viewPos = mul(position, view);
    float4 projPos = mul(viewPos, proj);
    projPos.xyz /= projPos.w;
    
    if (projPos.z <= pixelDepth) // 저장되어 있는 값이 더 작은 depth라면 무시함
        discard;
    
    return pixelDepth;
}