#include "Common.hlsli"

Texture2DMS<float4, SAMPLE_COUNT> normalEdgeTex : register(t0);
Texture2DMS<float4, SAMPLE_COUNT> positionTex : register(t1);
Texture2DMS<uint, SAMPLE_COUNT> coverageTex : register(t2);

cbuffer SsaoConstantBuffer : register(b0)
{
    float4 sampleKernel[16];
}

cbuffer SsaoNoiseConstantBuffer : register(b1)
{
    float4 rotationNoise[16];
}

struct psInput
{
    float4 posProj : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

float2 texcoordToScreen(float2 texcoord, float width, float height)
{
    return float2(texcoord.x * (width - 1.0) + 0.5, texcoord.y * (height - 1.0) + 0.5);
}

float getOcclusionFactor(float2 texcoord, float3 viewPos, float3 viewNormal)
{
    // linear Wrap Sampler로 랜덤 회전 벡터 얻기
    // 2x2px -> same random vector
    float fx = frac(texcoord.x * appWidth) * 3.0; // [0,3)
    float fy = frac(texcoord.y * appHeight) * 3.0; // [0,3)
    
    uint fx1 = uint(floor(fx));
    uint fx2 = uint(floor(fx + 1.0));
    uint fy1 = uint(floor(fy));
    uint fy2 = uint(floor(fy + 1.0));
    
    float3 v1 = lerp(rotationNoise[fx1 + 4 * fy1].xyz, rotationNoise[fx2 + 4 * fy1].xyz, frac(fx));
    float3 v2 = lerp(rotationNoise[fx1 + 4 * fy2].xyz, rotationNoise[fx2 + 4 * fy2].xyz, frac(fx));
    float3 randomVec = normalize(lerp(v1, v2, frac(fy)));
 
    float3 T = normalize(randomVec - viewNormal * dot(viewNormal, randomVec)); // R - proj.n(R)
    float3 B = cross(viewNormal, T);
    float3x3 TBN = float3x3(T, B, viewNormal);
    
    float occlusionFactor = 0.0;
    float radius = 1.5;
    float bias = 0.05;
    
    const uint COUNT = 16;
    [unroll]
    for (uint i = 0; i < COUNT; ++i)
    {
        float3 sampleOffset = mul(sampleKernel[i].xyz, TBN);
        float3 samplePos = viewPos + sampleOffset * radius;
        
        float4 sampleProjPos = float4(samplePos, 1.0);
        sampleProjPos = mul(sampleProjPos, proj);
        sampleProjPos.xyz /= sampleProjPos.w; // [-1, 1]
        
        float2 sampleTexcoord = sampleProjPos.xy;
        sampleTexcoord.x = saturate(sampleTexcoord.x * 0.5 + 0.5); // [-1, 1] -> [0, 1]
        sampleTexcoord.y = saturate(-(sampleTexcoord.y * 0.5) + 0.5); // [-1, 1] -> [1, 0]
        
        float2 sampleScreenCoord = texcoordToScreen(sampleTexcoord, appWidth, appHeight);
        // SampleIndex 중 아무거나 하나 집어도 무관: 샘플의 위치가 다르다고 가정하면 됨
        float4 position = positionTex.Load(sampleScreenCoord, 0);
        float4 storedViewPos = mul(float4(position.xyz, 1.0), view);
        if (position.w == -1.0)
            storedViewPos.xyz = float3(0, 0, 1000.0);
        
        float w = smoothstep(0.0, 1.0, radius / max(1e-4, length(viewPos - storedViewPos.xyz)));
        float rangeCheck = pow(w, 2.0);
        
        occlusionFactor += (storedViewPos.z + bias < samplePos.z ? 1.0 : 0.0) * rangeCheck;
    }
    
    return occlusionFactor / float(COUNT);
}

float main(psInput input) : SV_TARGET
{   
    float3 worldNormal = normalEdgeTex.Load(input.posProj.xy, 0).xyz;
    if (length(worldNormal) == 0)
        return 0.0;
    float3 viewNormal = mul(float4(worldNormal, 0.0), view).xyz;
    viewNormal = normalize(viewNormal);
    
    float4 worldPos = positionTex.Load(input.posProj.xy, 0);
    if (worldPos.w == -1.0)
        return 0.0;
    float3 viewPos = mul(float4(worldPos.xyz, 1.0), view).xyz;
    
    float occlusionFactor = getOcclusionFactor(input.texcoord, viewPos.xyz, viewNormal);
    
    float distance = length(viewPos.xyz);
    float attenuation = saturate((lodRenderDistance - distance) / (lodRenderDistance - 32.0));
    
    return (occlusionFactor * attenuation);
}

float mainMSAA(psInput input) : SV_TARGET
{
    uint4 coverage;
    
    coverage.x = coverageTex.Load(input.posProj.xy, 0);
    coverage.y = coverageTex.Load(input.posProj.xy, 1);
    coverage.z = coverageTex.Load(input.posProj.xy, 2);
    coverage.w = coverageTex.Load(input.posProj.xy, 3);
    
    uint4 sampleWeight = coverageAnalysis(coverage);
    uint sampleWeightArray[4] = { sampleWeight.x, sampleWeight.y, sampleWeight.z, sampleWeight.w };
    
    sampleWeightArray[0] = 1;
    sampleWeightArray[1] = 1;
    sampleWeightArray[2] = 1;
    sampleWeightArray[3] = 1;
    
    float sumOcclusionFactor = 0.0;

    // dont use [unroll] -> continue statement
    [loop]
    for (uint i = 0; i < SAMPLE_COUNT; ++i) // loop max 4
    {
        if (sampleWeightArray[i] == 0)
            continue;
        
        float3 worldNormal = normalEdgeTex.Load(input.posProj.xy, i).xyz;
        if (length(worldNormal) == 0)
            continue;
        float3 viewNormal = mul(float4(worldNormal, 0.0), view).xyz;
        viewNormal = normalize(viewNormal);

        float4 worldPos = positionTex.Load(input.posProj.xy, i);
        if (worldPos.w == -1.0)
            continue;
        float3 viewPos = mul(float4(worldPos.xyz, 1.0), view).xyz;
        
        float occlusionFactor = getOcclusionFactor(input.texcoord, viewPos.xyz, viewNormal) * sampleWeightArray[i];
        
        float distance = length(viewPos.xyz);
        float attenuation = saturate((lodRenderDistance - distance) / (lodRenderDistance - 32.0));
        
        sumOcclusionFactor += occlusionFactor * attenuation;
    }
    
    sumOcclusionFactor /= SAMPLE_COUNT;
    
    return sumOcclusionFactor;
}