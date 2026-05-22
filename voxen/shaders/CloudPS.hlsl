#include "Common.hlsli"
#include "Lighting.hlsli"

cbuffer CloudConstantBuffer : register(b0)
{
    matrix world;
    float3 volumeColor;
    float cloudScale;
}

struct psInput
{
    float4 posProj : SV_POSITION;
    float3 posWorld : POSITION;
    uint face : FACE;
};

float3 getNormal(uint face)
{
    if (face == LEFT)
    {
        return float3(-1.0, 0.0, 0.0);
    }
    else if (face == RIGHT)
    {
        return float3(1.0, 0.0, 0.0);
    }
    else if (face == BOTTOM)
    {
        return float3(0.0, -1.0, 0.0);
    }
    else if (face == TOP)
    {
        return float3(0.0, 1.0, 0.0);
    }
    else if (face == NEAR)
    {
        return float3(0.0, 0.0, -1.0);
    }
    else // FAR
    {
        return float3(0.0, 0.0, 1.0);
    }
}

float4 main(psInput input) : SV_TARGET
{   
    // 바라보는 방향에 대한 anisotropy 
    float sunAniso = max(dot(lightDir, eyeDir), 0.0);
    float3 eyeHorizonColor = lerp(normalHorizonColor, sunHorizonColor, sunAniso);

    // 거리가 멀면 horizon color 선택 
    // distance 범위 clamp
    float distance = length(input.posWorld.xz - eyePos.xz);
    float clampedDistance = clamp(distance, maxRenderDistance, cloudScale);
    float horizonWeight = smoothstep(maxRenderDistance, cloudScale, clampedDistance);
    float albedo = lerp(volumeColor, eyeHorizonColor, horizonWeight);
    
    // ambient lighting
    float3 normal = getNormal(input.face);
    float3 ambientLighting = getAmbientLighting(1.0, albedo, input.posWorld, normal, 0.0, 0.75, false);
    
    // direct lighting
    float3 directLighting = getDirectLighting(normal, input.posWorld, albedo, 0.0, 0.75, false);
    
    // distance alpha
    float alphaWeight = 1.0 - smoothstep(maxRenderDistance, cloudScale, clampedDistance);
    float alpha = alphaWeight * 0.75; // [0, 0.75]
    
    return float4(ambientLighting + directLighting, alpha);
}