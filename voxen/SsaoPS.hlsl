#include "Common.hlsli"

Texture2DMS<float4, SAMPLE_COUNT> normalEdgeTex : register(t0);
Texture2DMS<float4, SAMPLE_COUNT> positionTex : register(t1);
Texture2DMS<uint, SAMPLE_COUNT> coverageTex : register(t2);

cbuffer SsaoConstantBuffer : register(b0)
{
    float4 sampleKernel[64];
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

float getOcclusionFactor(float2 texcoord, float3 viewPos, float3 normal)
{
    // 200배 확대한 것을 frac연산으로 다시 하나씩 200개로 쪼갬 -> 4로 곱하여 인덱스로 사용
    // PointWrap 샘플러라고 생각
    float fx = frac(texcoord.x * 200.0) * 4.0; // [0,4]
    float fy = frac(texcoord.y * 200.0) * 4.0; // [0,4]
    uint ix = uint(floor(fx)) % 4;
    uint iy = uint(floor(fy)) % 4;
    float3 randomVec = rotationNoise[ix + 4 * iy].xyz;
 
    float3 T = normalize(randomVec - normal * dot(normal, randomVec)); // R - proj.n(R)
    float3 B = cross(normal, T);
    float3x3 TBN = float3x3(T, B, normal);
    
    float occlusionFactor = 0.0;
    float radius = 0.5;
    float bias = 0.025;
    
    [unroll]
    for (uint i = 0; i < 64; ++i)
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
        
        float w = smoothstep(0.0, 1.0, radius / length(viewPos - storedViewPos.xyz));
        float rangeCheck = pow(w, 3.0);
        
        occlusionFactor += (storedViewPos.z < samplePos.z - bias ? 1.0 : 0.0) * rangeCheck;
    }
    
    return occlusionFactor / 64.0;
}

float main(psInput input) : SV_TARGET
{   
    float3 normal = normalEdgeTex.Load(input.posProj.xy, 0).xyz;
    if (length(normal) == 0)
        return 1.0;
    normal = mul(float4(normal, 0.0), view).xyz; // [invWorld * invView]
    normal = normalize(normal);
    
    float4 position = positionTex.Load(input.posProj.xy, 0);
    if (position.w == -1.0)
        return 1.0;
    position = mul(float4(position.xyz, 1.0), view);
    
    float occlusionFactor = getOcclusionFactor(input.texcoord, position.xyz, normal);
    
    float distance = length(position.xyz);
    float attenuation = saturate((lodRenderDistance - distance) / (lodRenderDistance - 32.0));
    
    return 1.0 - (occlusionFactor * attenuation);
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
    
    float sumOcclusionFactor = 0.0;
   
    // dont use [unroll] -> continue statement
    [loop]
    for (uint i = 0; i < SAMPLE_COUNT; ++i) // loop max 4
    {
        if (sampleWeightArray[i] == 0)
            continue;
        
        float3 normal = normalEdgeTex.Load(input.posProj.xy, i).xyz;
        if (length(normal) == 0)
            continue;
        normal = mul(float4(normal, 0.0), view).xyz;
        normal = normalize(normal);

        float4 position = positionTex.Load(input.posProj.xy, i);
        if (position.w == -1.0)
            continue;
        position = mul(float4(position.xyz, 1.0), view);
        
        float occlusionFactor = getOcclusionFactor(input.texcoord, position.xyz, normal) * sampleWeightArray[i];
        
        float distance = length(position.xyz);
        float attenuation = saturate((lodRenderDistance - distance) / (lodRenderDistance - 32.0));
        
        sumOcclusionFactor += occlusionFactor * attenuation;
    }
    
    sumOcclusionFactor /= SAMPLE_COUNT;
    
    return 1.0 - sumOcclusionFactor;
}