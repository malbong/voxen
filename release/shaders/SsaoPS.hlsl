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

uint4 coverageAnalysis(uint4 coverage)
{
    uint4 sampleWeight = uint4(1, 1, 1, 1);
    
    // coverage가 같으면 같은 Geometry로 판단하고 다른 coverage는 꺼버림
    
    // x
    if (coverage.x == coverage.y)
    {
        ++sampleWeight.x;
        coverage.y = 0;
    }
    if (coverage.x == coverage.z)
    {
        ++sampleWeight.x;
        coverage.z = 0;
    }
    if (coverage.x == coverage.w)
    {
        ++sampleWeight.x;
        coverage.w = 0;
    }
    
    // y
    if (coverage.y == coverage.z)
    {
        ++sampleWeight.y;
        coverage.z = 0;
    }
    if (coverage.y == coverage.w)
    {
        ++sampleWeight.y;
        coverage.w = 0;
    }
    
    // z
    if (coverage.z == coverage.w)
    {
        ++sampleWeight.z;
        coverage.w = 0;
    }
    
    // coverage가 꺼지지 않았는지 판단 후 꺼지지 않았다면 sampleWeight 그대로 사용
    sampleWeight.x = (coverage.x > 0) ? sampleWeight.x : 0;
    sampleWeight.y = (coverage.y > 0) ? sampleWeight.y : 0;
    sampleWeight.z = (coverage.z > 0) ? sampleWeight.z : 0;
    sampleWeight.w = (coverage.w > 0) ? sampleWeight.w : 0;
    
    return sampleWeight;
}

float2 texcoordToScreen(float2 texcoord, float width, float height)
{
    return float2(texcoord.x * (width - 1.0) + 0.5, texcoord.y * (height - 1.0) + 0.5);
}

float getOcclusionFactor(float2 screenPos, float3 viewPos, float3 viewNormal)
{
    // 4x4px -> same random vector
    // 4x4 pixel 마다 주기 반복
    uint ix = uint(screenPos.x) % 4;
    uint iy = uint(screenPos.y) % 4;
    float3 randomVec = normalize(rotationNoise[ix + 4 * iy].xyz);
 
    float3 T = normalize(randomVec - viewNormal * dot(viewNormal, randomVec)); // R - proj.n(R): 90도 초과 시 한쪽에 맺힘
    float3 B = cross(viewNormal, T);
    float3x3 TBN = float3x3(T, B, viewNormal);
    
    float occlusionFactor = 0.0;
    float radius = 1.5;
    float bias = 0.01;
    
    uint validSampleCount = 0;
    const float INVALID_POSITION = -1.0;
    const uint SSAO_SAMPLE_COUNT = 16;
    [unroll]
    for (uint i = 0; i < SSAO_SAMPLE_COUNT; ++i)
    {
        float3 sampleOffset = mul(sampleKernel[i].xyz, TBN);
        float3 samplePos = viewPos + sampleOffset * radius; // samplePos of viewspace
        
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
        if (position.w == INVALID_POSITION)
            storedViewPos.z = 1000.0;
        
        float diff = max(1e-4, length(viewPos - storedViewPos.xyz));
        float w = smoothstep(0.0, 1.0, radius / diff);
        float rangeWeight = pow(w, 2.0);
        
        // 저장되어 있는 값이 더 가까운 경우 차폐가 생김
        // 동일한 위치인 경우 저장되어 있는 값을 뒤로 밀어 차폐가 생기지 않게함 -> bias 더함
        occlusionFactor += (storedViewPos.z + bias < samplePos.z ? 1.0 : 0.0) * rangeWeight;
        validSampleCount++;
    }
    
    return occlusionFactor / float(validSampleCount);
}

float main(psInput input) : SV_TARGET
{   
    if (!useSSAO)
        return 0.0;
    
    float3 worldNormal = normalEdgeTex.Load(input.posProj.xy, 0).xyz;
    if (length(worldNormal) == 0)
        return 0.0;
    float3 viewNormal = mul(float4(worldNormal, 0.0), view).xyz;
    viewNormal = normalize(viewNormal);
    
    float4 worldPos = positionTex.Load(input.posProj.xy, 0);
    if (worldPos.w == -1.0)
        return 0.0;
    float3 viewPos = mul(float4(worldPos.xyz, 1.0), view).xyz;
    
    float occlusionFactor = getOcclusionFactor(input.posProj.xy, viewPos, viewNormal);
    
    float maxSSAODistance = CHUNK_SIZE * 3;
    float minSSAODistance = CHUNK_SIZE;
    float distance = length(viewPos.xyz);
    float attenuation = saturate((maxSSAODistance - distance) / (maxSSAODistance - minSSAODistance));
    
    return (occlusionFactor * attenuation);
}

float mainMSAA(psInput input) : SV_TARGET
{
    if (!useSSAO)
        return 0.0;
    
    // check semiAlpha masking
    const float SEMIALPHA_MASK = 2.0;
    uint semiAlphaCount = 0;
    [unroll]
    for (uint s = 0; s < SAMPLE_COUNT; ++s)
    {
        float ne_w = normalEdgeTex.Load(input.posProj.xy, s).w;
        if (ne_w == SEMIALPHA_MASK)
            ++semiAlphaCount;
    }
        
    // pixel 내의 semiAlphaCount의 개수가 0개 -> SampleWeight 활용
    // pixel 내의 semiAlhpaCount의 개수가 1-3개 -> SampleWeight 없이 반복
    // pixel 내의 semiAlphaCount의 개수가 4개 -> NonEdge
    uint sampleWeightArray[4] = { 1, 1, 1, 1 };
    if (semiAlphaCount == 0)
    {
        uint4 coverage;
        uint4 sampleWeight;
        
        coverage.x = coverageTex.Load(input.posProj.xy, 0);
        coverage.y = coverageTex.Load(input.posProj.xy, 1);
        coverage.z = coverageTex.Load(input.posProj.xy, 2);
        coverage.w = coverageTex.Load(input.posProj.xy, 3);
        
        sampleWeight = coverageAnalysis(coverage);
        sampleWeightArray[0] = sampleWeight.x;
        sampleWeightArray[1] = sampleWeight.y;
        sampleWeightArray[2] = sampleWeight.z;
        sampleWeightArray[3] = sampleWeight.w;
    }
    
    float sumOcclusionFactor = 0.0;
    uint validSampleCount = 0;
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
        
        float occlusionFactor = getOcclusionFactor(input.posProj.xy, viewPos, viewNormal) * sampleWeightArray[i];
        
        // attenuation [distance: 32, 96] => [attenuation: 1, 0]
        float maxSSAODistance = CHUNK_SIZE * 3;
        float minSSAODistance = CHUNK_SIZE;
        float distance = length(viewPos.xyz);
        float attenuation = saturate((maxSSAODistance - distance) / (maxSSAODistance - minSSAODistance));
        
        sumOcclusionFactor += occlusionFactor * attenuation;
        
        validSampleCount += sampleWeightArray[i];
    }
    
    if (validSampleCount == 0)
        return 0.0;
    
    sumOcclusionFactor /= validSampleCount;
    
    return sumOcclusionFactor;
}