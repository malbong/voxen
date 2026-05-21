#include "Common.hlsli"
#include "Lighting.hlsli"

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
    float2 texcoord : TEXCOORD;
    uint texIndex : INDEX;
};

float3 schlickFresnel(float3 N, float3 E, float3 F0)
{
    // https://en.wikipedia.org/wiki/Schlick%27s_approximation
    // [f0 ~ 1]
    // 90 -> dot(N,E)==0 -> f0+(1-f0)*1^5 -> 1
    //  0 -> dot(N,E)==1 -> f0+(1-f0)*0*5 -> f0
    return F0 + (1 - F0) * pow((1 - max(dot(N, E), 0.0)), 5.0);
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
    normalTex = normalize(2.0 * normalTex - 1.0); // TBN ????????? ??????? TBN ???
    
    // Water Plane?? ???????? ???? TBN??? ???? ???????? ??????? ????
    //  float3 T = float3(1.0, 0.0, 0.0); // T`
    //  float3 B = float3(0.0, 0.0, -1.0); // B`
    //  float3 N = float3(0.0, 1.0, 0.0); // N` 
    // (r, g, b) * TBNMatrix -> (r, b, -g)
    return float3(normalTex.r, normalTex.b, -normalTex.g);
}

float4 main(psInput input, uint sampleIndex : SV_SampleIndex) : SV_TARGET
{
    if (input.normal.y <= 0 || input.posWorld.y < 64.0 - 1e-4 || 64.0 + 1e-4 < input.posWorld.y)
        discard;

    // choose water Texture by date time
    uint waterStillTextureArraySize = 32;
    uint dateAmountPerSecond = dayCycleAmount / dayCycleRealTime; // 24000 / 30 -> 800
    uint dateAmountPerIndex = dateAmountPerSecond / waterStillTextureArraySize; // 800 / 32 -> 25
    uint waterStillTextureIndex = (dateTime % dateAmountPerSecond) / dateAmountPerIndex;
    
    // normal mapping and get roughness from dot(mappedNormal, input.normal)
    float3 mappedNormal = normalMapping(input.texcoord, waterStillTextureIndex);
    float roughness = 0.2 / max(dot(mappedNormal, input.normal), 1e-3);
    roughness = clamp(roughness, 0.0, 1.0);
    
    // water color
    float3 albedo = getWaterAlbedo(input.texcoord, waterStillTextureIndex, input.posWorld, mappedNormal);
    float3 ambientLighting = getAmbientLighting(1.0, albedo, input.posWorld, mappedNormal, 0.0, roughness, true);
    float3 directLighting = getDirectLighting(mappedNormal, input.posWorld, albedo, 0.0, roughness, true);
    float3 waterColor = ambientLighting + directLighting;
        
    // projection render color
    float3 projectedColor = msaaRenderTex.Load(input.posProj.xy, sampleIndex).rgb;
    if (isUnderWater)
    {
        return float4(lerp(projectedColor, waterColor, 0.5), 1.0);
    }
    else
    {
        // absorption -> mix(projectedColor, waterColor, absorptionFactor)
        float3 projectedObjectPosition = positionTex.Load(input.posProj.xy, sampleIndex).xyz;
        float planeToProjectionObjectDistance = length(input.posWorld - projectedObjectPosition);
        float waterAbsorptionCoeff = 0.075;
        float waterAbsorptionFactor = 1.0 - exp(-waterAbsorptionCoeff * planeToProjectionObjectDistance); // beer-lambert
        float3 eyeToWaterPlaneColor = lerp(projectedColor, waterColor, waterAbsorptionFactor);
    
        // reflect color
        float2 screenTexcoord = float2(input.posProj.x / appWidth, input.posProj.y / appHeight);
        float3 mirrorColor = mirrorWorldTex.Sample(linearClampSS, screenTexcoord).rgb;
        
        // fresnel factor
        float3 planeToEye = normalize(eyePos - input.posWorld);
        float3 reflectCoeff = float3(0.02, 0.02, 0.02); // fresnel 값의 최소
        float3 fresnelFactor = schlickFresnel(mappedNormal, planeToEye, reflectCoeff);
        
        // blending 3 colors
        float3 blendColor = lerp(eyeToWaterPlaneColor, mirrorColor, fresnelFactor);
        
        return float4(blendColor, 1.0);
    }
}