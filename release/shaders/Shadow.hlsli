#ifndef SHADOW_HLSLI
    #define SHADOW_HLSLI

#include "Common.hlsli"

#define BLEND_CASCADE (cascadeLevel + 1)
#define BLEND_LAST_CASCADE (cascadeLevel + 2)

Texture2DArray shadowTex : register(t13);

float3 getCascadeColor(uint cascade)
{
    float3 cascadeColor = float3(0.0, 0.0, 0.0);
    
    if (cascade == 0)
        cascadeColor = float3(1, 0, 0);
    else if (cascade == 1)
        cascadeColor = float3(0, 1, 0);
    else if (cascade == 2)
        cascadeColor = float3(0, 0, 1);
    else if (cascade == BLEND_CASCADE)
        cascadeColor = float3(1, 1, 0);
    else if (cascade == BLEND_LAST_CASCADE)
        cascadeColor = float3(0.5, 0.5, 0.5);
    else
        cascadeColor = float3(0.25, 0.25, 0.25);
    
    return cascadeColor;
}

float getCascadeBias(uint cascadeIndex, float NdotL)
{
    const float baseBias[3] = { 0.0025, 0.002, 0.002 };
    const float slopeBias[3] = { 0.005, 0.005, 0.002 };
    
    return baseBias[cascadeIndex] + slopeBias[cascadeIndex] * pow(1.0 - NdotL, 3.0);
}

float sampleCascade(float4 proj, uint cascadeIndex, float bias)
{
    float2 lightTexcoord = float2(proj.x * 0.5 + 0.5, -proj.y * 0.5 + 0.5);
    
    return shadowTex.SampleCmpLevelZero(
        shadowCompareSS, float3(lightTexcoord, cascadeIndex), proj.z - bias).r;
}

void mapBasedCascadeSelection(float3 posWorld, float NdotL, inout uint outCascade, out float4 outLightProj)
{
    [unroll]
    for (uint i = 0; i < cascadeLevel; ++i)
    {
        float bias = getCascadeBias(i, NdotL);
        
        float4 lightProj = mul(float4(posWorld, 1.0), shadowViewProj[i]);
        lightProj.xyz /= lightProj.w;

        if (all(abs(lightProj.xy) < 1.0) && bias < lightProj.z && lightProj.z < 1.0)
        {
            outCascade = i;
            outLightProj = lightProj;
            break;
        }
    }
}

void intervalBasedCascadeSelection(float3 posWorld, float NdotL, inout uint outCascade, out float4 outLightProj)
{
    float viewDistZ = dot(posWorld - eyePos, eyeDir); // viewZ: |posWorld-eyePos| * |eyeDir| * cosTheta
    float cascadeDistance[4] = { cascadeSplits.x, cascadeSplits.y, cascadeSplits.z, cascadeSplits.w };
    
    [unroll]
    for (uint i = 0; i < cascadeLevel; ++i)
    {
        if (cascadeDistance[i] <= viewDistZ && viewDistZ < cascadeDistance[i + 1])
        {
            outCascade = i;
            
            float4 lightProj = mul(float4(posWorld, 1.0), shadowViewProj[i]);
            lightProj.xyz /= lightProj.w;
            
            outLightProj = lightProj;
            break;
        }
    }
}

bool blendMapBased(float blendRange, uint cascadeIndex, float3 posWorld, float4 proj, float NdotL,
                        inout float inOutPercentLit, inout uint outCascadeIndex)
{
    float NdcDistXY = min(min(proj.x + 1.0, 1.0 - proj.x),
                               min(proj.y + 1.0, 1.0 - proj.y)); // NDC.xy 중앙(1,1)->1, 측면->0
    float NdcDistZ = min(proj.z, 1.0 - proj.z) * 2.0; // NDC.z 중앙(0.5)->1
    float NdcDist = min(NdcDistXY, NdcDistZ);
    
    float blendWeight = 1.0 - smoothstep(0.0, blendRange, NdcDist);
        
    if (blendWeight > 0.0)
    {
        uint nextCascadeIndex = cascadeIndex + 1;
        
        if (nextCascadeIndex == cascadeLevel)
        {
            inOutPercentLit = lerp(inOutPercentLit, 1.0, blendWeight);
            outCascadeIndex = BLEND_LAST_CASCADE;
            return true;
        }
        
        float nextBias = getCascadeBias(nextCascadeIndex, NdotL);
        
        float4 nextLightProj = mul(float4(posWorld, 1.0), shadowViewProj[nextCascadeIndex]);
        nextLightProj.xyz /= nextLightProj.w;

        if (all(abs(nextLightProj.xy) < 1.0) && nextBias < nextLightProj.z && nextLightProj.z < 1.0)
        {
            float nextPercentLit = sampleCascade(nextLightProj, nextCascadeIndex, nextBias);

            inOutPercentLit = lerp(inOutPercentLit, nextPercentLit, blendWeight);
            
            outCascadeIndex = BLEND_CASCADE;
            
            return true;
        }
    }
    
    return false;
}

bool blendIntervalBased(float blendRange, uint cascadeIndex, float3 posWorld, float NdotL,
                            inout float inOutPercentLit, inout uint outCascadeIndex)
{
    float viewDistZ = dot(posWorld - eyePos, eyeDir); // viewZ: |posWorld-eyePos| * |eyeDir| * cosTheta
    float cascadeDistance[4] = { cascadeSplits.x, cascadeSplits.y, cascadeSplits.z, cascadeSplits.w };
    
    float nearZ = cascadeDistance[cascadeIndex];
    float farZ = cascadeDistance[cascadeIndex + 1];
    float blendStartZ = lerp(farZ, nearZ, blendRange);
        
    // viewDistZ가 blendStartZ 보다 작거나 같은 경우 blending을 하지 않게 됨
    float blendWeight = smoothstep(blendStartZ, farZ, viewDistZ);
    
    if (blendWeight > 0.0)
    {
        uint nextCascadeIndex = cascadeIndex + 1;
        
        if (nextCascadeIndex == cascadeLevel)
        {
            inOutPercentLit = lerp(inOutPercentLit, 1.0, blendWeight);
            outCascadeIndex = BLEND_LAST_CASCADE;
            return true;
        }
        
        float nextBias = getCascadeBias(nextCascadeIndex, NdotL);
        
        float4 nextLightProj = mul(float4(posWorld, 1.0), shadowViewProj[nextCascadeIndex]);
        nextLightProj.xyz /= nextLightProj.w;
        
        float nextPercentLit = sampleCascade(nextLightProj, nextCascadeIndex, nextBias);
        inOutPercentLit = lerp(inOutPercentLit, nextPercentLit, blendWeight);
            
        outCascadeIndex = BLEND_CASCADE;
            
        return true;
    }
    
    return false;
}

float getShadowFactor(float3 posWorld, float3 normal, out uint outCascadeIndex)
{
    float NdotL = max(dot(lightDir, normal), 0.0);
    
    uint selectedCascade = cascadeLevel;
    float4 selectedCascadeLightProj;
    
    /*
    * 1. NDC를 기준으로 Cascade를 찾는 Map Based Cascade
    * 2. interval(depth, viewDistZ)를 가지고 Cascade를 찾는 Interval Based Cascade
    */
    if (useMapBasedCascade)
        mapBasedCascadeSelection(posWorld, NdotL, selectedCascade, selectedCascadeLightProj);
    else
        intervalBasedCascadeSelection(posWorld, NdotL, selectedCascade, selectedCascadeLightProj);
    
    outCascadeIndex = selectedCascade;
    
    // 적절한 cascade를 찾지 못한 경우 얼리리턴
    if (selectedCascade == cascadeLevel)
    {
        return 1.0;
    }
        
    
    float bias = getCascadeBias(selectedCascade, NdotL);
    float percentLit = sampleCascade(selectedCascadeLightProj, selectedCascade, bias);
    
    // blending cascade
    if (useCascadeBlend)
    {
        /*
        * blendMapBased: NDC 거리를 이용하여 BlendWeight 결정
        * blendIntervalBased: view Dist Z 값을 이용하여 BlendWeight 결정
        */
        if (useMapBasedCascade)
            blendMapBased(0.2, selectedCascade, posWorld, selectedCascadeLightProj, NdotL, percentLit, outCascadeIndex);
        else
            blendIntervalBased(0.2, selectedCascade, posWorld, NdotL, percentLit, outCascadeIndex);
    }
    
    float radianceShadowWeight = clamp(radianceWeight / maxRadianceWeight, 0.0, 1.0);
    return lerp(percentLit, 1.0, 1.0 - radianceShadowWeight);
}

#endif