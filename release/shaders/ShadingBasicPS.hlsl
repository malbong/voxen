#include "Common.hlsli"

Texture2DMS<float4, SAMPLE_COUNT> normalEdgeTex : register(t0);
Texture2DMS<float4, SAMPLE_COUNT> positionTex : register(t1);
Texture2DMS<float4, SAMPLE_COUNT> albedoTex : register(t2);
Texture2DMS<float4, SAMPLE_COUNT> merTex : register(t3);
Texture2D ssaoTex : register(t4);

struct psInput
{
    float4 posProj : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

float4 main(psInput input) : SV_TARGET
{
    float3 normal = float3(0.0, 0.0, 0.0); // Texture Normal Edge 처리 -> 평균값
    normal += normalEdgeTex.Load(input.posProj.xy, 0).xyz;
    normal += normalEdgeTex.Load(input.posProj.xy, 1).xyz;
    normal += normalEdgeTex.Load(input.posProj.xy, 2).xyz;
    normal += normalEdgeTex.Load(input.posProj.xy, 3).xyz;
    normal /= SAMPLE_COUNT;
    
    float3 position = float3(0.0, 0.0, 0.0);
    position += positionTex.Load(input.posProj.xy, 0).xyz;
    position += positionTex.Load(input.posProj.xy, 1).xyz;
    position += positionTex.Load(input.posProj.xy, 2).xyz;
    position += positionTex.Load(input.posProj.xy, 3).xyz;
    position /= SAMPLE_COUNT;
    
    float3 albedo = float3(0.0, 0.0, 0.0); // Texture Albedo Edge 처리 -> 평균값
    albedo += albedoTex.Load(input.posProj.xy, 0).rgb;
    albedo += albedoTex.Load(input.posProj.xy, 1).rgb;
    albedo += albedoTex.Load(input.posProj.xy, 2).rgb;
    albedo += albedoTex.Load(input.posProj.xy, 3).rgb;
    albedo /= SAMPLE_COUNT;
    
    float3 mer = float3(0.0, 0.0, 0.0); // Texture MER Edge 처리 -> 평균값
    mer += merTex.Load(input.posProj.xy, 0).rgb;
    mer += merTex.Load(input.posProj.xy, 1).rgb;
    mer += merTex.Load(input.posProj.xy, 2).rgb;
    mer += merTex.Load(input.posProj.xy, 3).rgb;
    mer /= SAMPLE_COUNT;
    
    float metallic = mer.r;
    float roughness = mer.b;
    
    float ao = ssaoTex.Sample(pointClampSS, input.texcoord).r;
    ao = pow(ao, 4.0);
    
    float3 ambientLighting = getAmbientLighting(ao, albedo, position.xyz, normal, metallic, roughness);
    float3 directLighting = getDirectLighting(normal, position.xyz, albedo, metallic, roughness, true);
    
    float3 lighting = ambientLighting + directLighting;
    float3 clampLighting = clamp(lighting, 0.0f, 1000.0f);
    
    return float4(clampLighting, 1.0);
}

float4 mainMSAA(psInput input) : SV_TARGET
{   
    float3 sumClampLighting = float3(0.0, 0.0, 0.0);
    
    uint vaildSampleCount = 0;
    
    [unroll]
    for (uint i = 0; i < SAMPLE_COUNT; ++i)
    {
        // point clamp를 이용해서 albedo를 구성했기 때문에 다른 샘플사이에서 다른 컬러를 사용할 가능성이 높음
        // sampleWeight를 사용하지 않음
        float4 position = positionTex.Load(input.posProj.xy, i);
        if (position.w == -1.0)
            continue;
        
        vaildSampleCount++;
        
        float3 normal = normalEdgeTex.Load(input.posProj.xy, i).xyz;
        
        float3 albedo = albedoTex.Load(input.posProj.xy, i).rgb; 
        
        float3 mer = merTex.Load(input.posProj.xy, i).rgb;
        
        // todo
        float metallic = mer.r;
        float roughness = mer.b;
        
        float ao = ssaoTex.Sample(pointClampSS, input.texcoord).r;
        ao = pow(ao, 4.0);
        
        float3 ambientLighting = getAmbientLighting(ao, albedo, position.xyz, normal, metallic, roughness);
        float3 directLighting = getDirectLighting(normal, position.xyz, albedo, metallic, roughness, true);
        
        float3 lighting = ambientLighting + directLighting;
        float3 clampLighting = clamp(lighting, 0.0f, 1000.0f);
        sumClampLighting += clampLighting;
    }
    return float4(1, 0, 0,1);
    return float4(sumClampLighting / max(1e-3, vaildSampleCount), 1.0);
}