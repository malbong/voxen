#include "Common.hlsli"

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

float4 main(psInput input) : SV_TARGET
{   
    // distance 범위 clamp
    float distance = length(input.posWorld.xz - eyePos.xz);
    float clampedDistance = clamp(distance, maxRenderDistance, cloudScale);
    
    // 거리가 멀면 horizon color 선택 
    float horizonWeight = smoothstep(maxRenderDistance, cloudScale, clampedDistance);
    
    // 바라보는 방향에 대한 anisotropy 
    float sunAniso = max(dot(lightDir, eyeDir), 0.0);
    float3 eyeHorizonColor = lerp(normalHorizonColor, sunHorizonColor, sunAniso);
    float albedo = lerp(volumeColor, eyeHorizonColor, horizonWeight);
    
    // ambient lighting
    float3 normal = getNormal(input.face);
    float3 ambientLighting = getAmbientLighting(1.0, albedo, input.posWorld, normal, 0.0, 0.75);
    
    // direct lighting
    float3 directLighting = getDirectLighting(normal, input.posWorld, albedo, 0.0, 0.75, false);
    
    // distance alpha
    float alphaWeight = 1.0 - smoothstep(maxRenderDistance, cloudScale, clampedDistance);
    float alpha = alphaWeight * 0.75; // [0, 0.75]
    
    return float4(ambientLighting + directLighting, alpha);
}