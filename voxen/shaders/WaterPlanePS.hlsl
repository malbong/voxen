#include "Common.hlsli"

Texture2DMS<float4, SAMPLE_COUNT> msaaRenderTex : register(t0);
Texture2D mirrorWorldTex : register(t1);
Texture2DMS<float4, SAMPLE_COUNT> positionTex : register(t2);
Texture2D waterColorMapTex : register(t3);
Texture2D climateNoiseMapTex : register(t4);
Texture2DArray waterStillAtlasTextureArray : register(t5);
Texture2DArray waterStillNormalAtlasTextureArray : register(t6);

struct psInput
{
    float4 posProj : SV_POSITION;
    float3 posWorld : POSITION;
    float3 normal : NORMAL;
    sample float2 texcoord : TEXCOORD;
    uint texIndex : INDEX;
};

float3 schlickFresnel(float3 N, float3 E, float3 R)
{
    // https://en.wikipedia.org/wiki/Schlick%27s_approximation
    // [f0 ~ 1]
    // 90 -> dot(N,E)==0 -> f0+(1-f0)*1^5 -> 1
    //  0 -> dot(N,E)==1 -> f0+(1-f0)*0*5 -> f0
    return R + (1 - R) * pow((1 - max(dot(N, E), 0.0)), 5.0);
}

float3 getWaterAlbedo(float2 texcoord, uint stillIndex, float3 worldPos, float3 normal)
{
    float alpha = waterStillAtlasTextureArray.Sample(pointWrapSS, float3(texcoord, stillIndex)).r;
    
    float3 faceBiasPos = -normal * 1e-4;
    
    float2 diffOffsetPos = floor(worldPos.xz + faceBiasPos.xz) - floor(eyePos.xz);
    
    float texelSize = 1.0 / (CHUNK_COUNT * CHUNK_SIZE);
    float2 climateTexcoord = float2(0.5 + diffOffsetPos.x * texelSize, 0.5 - diffOffsetPos.y * texelSize);
    climateTexcoord += float2(texelSize * 0.5, texelSize * 0.5);
    
    float2 th = climateNoiseMapTex.SampleLevel(pointClampSS, climateTexcoord, 0.0).rg;
    
    float3 waterColor = waterColorMapTex.SampleLevel(pointClampSS, float2(th.x, 1.0 - th.y), 0.0).rgb;
    
    waterColor *= alpha;
    
    return waterColor;
}

float3 normalMapping(float2 texcoord, uint stillIndex)
{
    float3 normalTex = waterStillNormalAtlasTextureArray.Sample(pointWrapSS, float3(texcoord, stillIndex)).rgb;
    normalTex = normalize(2.0 * normalTex - 1.0); // TBN 스페이스에 존재하는 TBN 좌표
    
    // Water Plane이 고정이므로 직접 TBN행렬 곱을 진행했다고 가정하고 리턴
    //  float3 T = float3(1.0, 0.0, 0.0); // T`
    //  float3 B = float3(0.0, 0.0, -1.0); // B`
    //  float3 N = float3(0.0, 1.0, 0.0); // N` 
    // (r, g, b) * TBNMatrix -> (r, b, -g)
    return float3(normalTex.r, normalTex.b, -normalTex.g);
}

float4 main(psInput input, uint sampleIndex : SV_SampleIndex) : SV_TARGET
{
    uint waterStillTextureArraySize = 32;
    uint dateAmountPerSecond = dayCycleAmount / dayCycleRealTime; // 24000 / 30 -> 800
    uint dateAmountPerIndex = dateAmountPerSecond / waterStillTextureArraySize; // 800 / 32 -> 25
    uint waterStillTextureIndex = (dateTime % dateAmountPerSecond) / dateAmountPerIndex;
    
    float3 normal = input.normal;
    if (normal.y <= 0 || input.posWorld.y < 64.0 - 1e-4 || 64.0 + 1e-4 < input.posWorld.y)
        discard;
    
    float3 mappedNormal = normalMapping(input.texcoord, waterStillTextureIndex);
    float roughness = 0.02 / max(dot(normal, input.normal), 1e-3);
    
    // absorption color
    float3 albedo = getWaterAlbedo(input.texcoord, waterStillTextureIndex, input.posWorld, normal);
    
    float3 ambientLighting = getAmbientLighting(1.0, albedo, input.posWorld, mappedNormal, 0.0, roughness);
    
    float3 directLighting = getDirectLighting(mappedNormal, input.posWorld, albedo, 0.0, roughness, true);
    
    float3 waterColor = ambientLighting + directLighting;
    
    float2 screenTexcoord = float2(input.posProj.x / appWidth, input.posProj.y / appHeight);
        
    // origin render color
    float3 originColor = msaaRenderTex.Load(input.posProj.xy, sampleIndex).rgb;
   
    if (isUnderWater)
    {
        return float4(lerp(originColor, waterColor, 0.5), 1.0);
    }
    else
    {
        // absorption factor
        float3 originPosition = positionTex.Load(input.posProj.xy, sampleIndex).xyz;
        float objectDistance = length(eyePos - originPosition);
        float planeDistance = length(eyePos - input.posWorld);
        float diffDistance = abs(objectDistance - planeDistance);
        float absorptionCoeff = 0.075;
        float absorptionFactor = 1.0 - exp(-absorptionCoeff * diffDistance); // beer-lambert
    
        float3 projColor = lerp(originColor, waterColor, absorptionFactor);
    
        // reflect color
        float3 mirrorColor = mirrorWorldTex.Sample(linearClampSS, screenTexcoord).rgb;
        
        // fresnel factor
        float3 toEye = normalize(eyePos - input.posWorld);
        float3 reflectCoeff = float3(0.2, 0.2, 0.2);
        float3 fresnelFactor = schlickFresnel(mappedNormal, toEye, reflectCoeff);
        
        // blending 3 colors
        projColor *= (1.0 - fresnelFactor);
        float3 blendColor = lerp(projColor, mirrorColor, fresnelFactor);
        
        return float4(blendColor, 1.0);
    }
}