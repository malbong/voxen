#include "Common.hlsli"

Texture2D samplingTexture : register(t0);
Texture2D worldPointTexture : register(t1);

struct psInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

float4 getWorldPointColor(float2 texcoord, float size)
{
    float4 worldPointColor = float4(0, 0, 0, 0);
    
    float2 pos = float2(0.5f, 0.5f) - texcoord;
    float padding = 0.005;
    if (length(pos) < size + padding)
    {
        float3 N = float3(0, 1.0, 0);
        float3 B = normalize(float3(eyeDir.x, 0.0, -eyeDir.z));
        float3 T = cross(B, N);
        float3x3 TBNMatrix = float3x3(T, B, N);
        
        float3 vTBN = mul(float3(pos.x, 0.0, pos.y), transpose(TBNMatrix));
        
        float2 worldPointTexcoord = float2(0.5 + vTBN.x * (0.5 / size), 0.5 + vTBN.y * (0.5 / size));
        
        worldPointColor = worldPointTexture.SampleLevel(pointClampSS, worldPointTexcoord, 0, 0);
    }
    
    return worldPointColor;
}

float4 main(psInput input) : SV_TARGET
{
    float4 biomeMap = samplingTexture.SampleLevel(linearWrapSS, input.texcoord, 0.0);
    
    float4 worldPointColor = getWorldPointColor(input.texcoord, 0.0125);
    
    return lerp(biomeMap, worldPointColor, worldPointColor.a);
}