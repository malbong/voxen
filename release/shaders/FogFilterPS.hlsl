#include "Common.hlsli"

Texture2DMS<float4, SAMPLE_COUNT> renderTex : register(t0);
Texture2DMS<float, SAMPLE_COUNT> depthTex : register(t1);

cbuffer FogConstantBuffer : register(b0)
{
    float fogDistMin;
    float fogDistMax;
    float fogStrength;
    float dummy;
};

struct psInput
{
    float4 posProj : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

float3 getFogColor(float3 lightDir, float3 eyeDir)
{
    float dirWeight = max(dot(lightDir, eyeDir), 0.0);
    
    float3 fogColor = lerp(normalHorizonColor, sunHorizonColor, dirWeight);
    if (isUnderWater)
        fogColor = normalZenithColor;
    
    return fogColor;
}

float getFogFactor(float3 pos)
{
    //Beer-Lambert law
    float dist = length(pos.xyz);
        
    float distFog = saturate((dist - fogDistMin) / (fogDistMax - fogDistMin));
    float fogFactor = exp(-fogStrength * distFog);
    
    return fogFactor;
} 

float4 main(psInput input, uint sampleIndex : SV_SampleIndex) : SV_TARGET
{
    float3 fogColor = getFogColor(lightDir, eyeDir);
    float3 renderColor = renderTex.Load(input.posProj.xy, sampleIndex).rgb;
    
    float depth = depthTex.Load(input.posProj.xy, sampleIndex).r;
    float3 viewPos = texcoordToViewPos(input.texcoord, depth);
    
    float fogFactor = getFogFactor(viewPos);
    
    float3 blendColor = lerp(fogColor, renderColor, fogFactor);
    
    return float4(blendColor, 1.0);
}
