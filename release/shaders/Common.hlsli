#ifndef COMMON_HLSLI
#define COMMON_HLSLI

#define PI 3.14159265
#define SAMPLE_COUNT 4

#define CHUNK_SIZE 32
#define CHUNK_COUNT 17

SamplerState pointWrapSS : register(s0);
SamplerState linearWrapSS : register(s1);
SamplerState pointClampSS : register(s2);
SamplerState linearClampSS : register(s3);
SamplerComparisonState shadowCompareSS : register(s4);

Texture2D brdfTex : register(t10);
Texture2D shadowTex : register(t11);

cbuffer AppConstantBuffer : register(b7)
{
    float appWidth;
    float appHeight;
    float mirrorWidth;
    float mirrorHeight;
}

cbuffer CameraConstantBuffer : register(b8)
{
    Matrix view;
    Matrix proj;
    Matrix invProj;
    float3 eyePos;
    float maxRenderDistance;
    float3 eyeDir;
    float lodRenderDistance;
    bool isUnderWater;
    float3 cameraDummyData;
};

cbuffer SkyboxConstantBuffer : register(b9)
{
    float3 normalHorizonColor;
    float skyScale;
    float3 normalZenithColor;
    float skyboxDummyData1;
    float3 sunHorizonColor;
    float skyboxDummyData2;
    float3 sunZenithColor;
    float skyboxDummyData3;
};

cbuffer LightConstantBuffer : register(b10)
{
    float3 lightDir;
    float radianceWeight;
    float3 radianceColor;
    float maxRadianceWeight;
}

cbuffer ShadowConstantBuffer : register(b11)
{
    Matrix shadowViewProj[3];
    float4 topLX;
    float4 viewPortW;
}

cbuffer DateConstantBuffer : register(b12)
{
    uint days;
    uint dateTime;
    uint dayCycleRealTime;
    uint dayCycleAmount;
}

float3 sRGB2Linear(float3 color)
{
    return pow(clamp(color, 0.0, 1.0), 2.2);
}

float3 texcoordToViewPos(float2 texcoord, float projDepth)
{
    float4 posProj;
    
    posProj.xy = texcoord * 2.0 - 1.0;
    posProj.y *= -1;
    posProj.z = projDepth;
    posProj.w = 1.0;

    float4 posView = mul(posProj, invProj);
    posView.xyz /= posView.w;
    
    return posView.xyz;
}

uint4 coverageAnalysis(uint4 coverage)
{
    uint4 sampleWeight = uint4(1, 1, 1, 1);
    
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
    if (coverage.z == coverage.w)
    {
        ++sampleWeight.z;
        coverage.w = 0;
    }

    // 재조정 : coverage가 0인 것은 마스킹이 안된 샘플이거나, 같은 마스킹이 있는 경우
    sampleWeight.x = (coverage.x > 0) ? sampleWeight.x : 0;
    sampleWeight.y = (coverage.y > 0) ? sampleWeight.y : 0;
    sampleWeight.z = (coverage.z > 0) ? sampleWeight.z : 0;
    sampleWeight.w = (coverage.w > 0) ? sampleWeight.w : 0;
    
    return sampleWeight;
}

// https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
// F, G, D 함수
float3 schlickFresnel(float3 F0, float NdotH)
{
    return F0 + (1 - F0) * pow(2, (-5.55473 * (NdotH) - 6.98316) * NdotH);
}

float ndfGGX(float NdotH, float roughness)
{
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    return alpha2 / max(1e-5, (3.141592 * pow(NdotH * NdotH * (alpha2 - 1) + 1, 2)));
}

float schlickGGX(float NdotI, float NdotO, float roughness)
{
    float k = (roughness + 1) * (roughness + 1) / 8;
    float gl = NdotI / (NdotI * (1 - k) + k);
    float gv = NdotO / (NdotO * (1 - k) + k);
    return gl * gv;
}

float3 getAmbientColor()
{
    float sunAniso = max(dot(lightDir, eyeDir), 0.0);
    float3 eyeHorizonColor = lerp(normalHorizonColor, sunHorizonColor, sunAniso);
    
    float3 ambientColor = float3(1.0, 1.0, 1.0);
    float sunAltitude = lightDir.y;
    float dayAltitude = PI / 12.0;
    float maxHorizonAltitude = -PI / 24.0;
    if (sunAltitude <= dayAltitude)
    {
        float w = smoothstep(maxHorizonAltitude, dayAltitude, sunAltitude);
        ambientColor = lerp(eyeHorizonColor, ambientColor, w);
    }
    
    return ambientColor;
}

float3 getDiffuseTerm(float3 albedo, float3 pixelToEye, float3 normal, float metallic)
{
    float3 Fdielectric = float3(0.04, 0.04, 0.04);
    float3 F0 = lerp(Fdielectric, albedo, metallic);
    float3 F = schlickFresnel(F0, max(0.0, dot(normal, pixelToEye)));
    
    float3 kd = lerp(1.0 - F, 0.0, metallic);
    
    // 언리얼 PBR 코스노트 코드
    // float3 diffuseIrradiance = irradianceIBLTex.Sample(linearSampler, normalWorld);
    // 내 코드와 이유
    // float3 diffuseIrradiance = (radianceColor * max(dot(normal, lightDir), 0.0)) + getAmbientColor();
    // - diffuseIBL은 모든 방향에서 오는 간접광에 대한 diffuse를 모아 둔 것
    // - 기본적으로 밝은 이미지에 노멀 방향이 라이트 방향과 유사하면 다른 곳 대비 더 밝음
    // - 그래서 기본색(getAmbientColor)에 노멀방향에 대한 라이트색을 "더해서" 더 밝게 표현
    // - roughness는 사용되지 않음 -> 모든 방향에서 오는 빛을 모으는 과정이라 결국 모든 방향에서 더하면 거칠든 매끄럽든 동일하다는 가정
    float3 diffuseIrradiance = (radianceColor * max(dot(normal, lightDir), 0.0)) + getAmbientColor();
    
    return kd * albedo * diffuseIrradiance;
}

float3 getSpecularTerm(float3 albedo, float3 pixelToEye, float3 normal, float metallic, float roughness)
{
    float2 specularBRDF = brdfTex.Sample(pointClampSS, float2(dot(pixelToEye, normal), 1 - roughness)).rg;
    
    // 언리얼 PBR 코스노트 코드
    // float3 specularIrradiance = specularIBLTex.SampleLevel(linearSampler, reflect(-pixelToEye, normal), roughness * 5.0f).rgb;
    // 내 코드와 이유
    // float3 specularIrradiance = lerp(reflectRadiance, ambientColor, roughness);
    // - 모든 방향에서 오는 간접광에 대한 specular를 모아 둔 것
    // - 코스노트 코드는 반사 방향에 대해 샘플링하고 거칠기에 대해서는 더 뿌옇게 표현함
    // - 즉, 거칠기가 적으면 반대 방향의 환경맵과 유사한 색을 샘플링, 반대로 거칠기가 크면 주변광을 샘플링
    //   ->lerp(radianceColor, getAmbientColor(), roughness)
    float3 ambientColor = getAmbientColor();
    float3 reflectDir = normalize(reflect(-pixelToEye, normal));
    float reflectRadianceWeight = max(dot(reflectDir, lightDir), 0.0);
    float3 reflectRadiance = lerp(ambientColor, radianceColor, reflectRadianceWeight);
    float3 specularIrradiance = lerp(reflectRadiance, ambientColor, roughness);
    
    float3 Fdielectric = float3(0.04, 0.04, 0.04);
    float3 F0 = lerp(Fdielectric, albedo, metallic);

    return specularIrradiance * (specularBRDF.r * F0 + specularBRDF.g);
}

float3 getAmbientLighting(float ao, float3 albedo, float3 position, float3 normal, float metallic, float roughness)
{  
    float3 pixelToEye = normalize(eyePos - position);
    
    float3 diffuseTerm = getDiffuseTerm(albedo, pixelToEye, normal, metallic);
    float3 specularTerm = getSpecularTerm(albedo, pixelToEye, normal, metallic, roughness);
    
    float weight = 0.75;
    return ao * (diffuseTerm + specularTerm) * weight;
}

float getShadowFactor(float3 posWorld, float3 normal)
{
    float width, height, numMips;
    shadowTex.GetDimensions(0, width, height, numMips);
    
    float topLXOffsets[3] = { topLX.x, topLX.y, topLX.z };
    float viewPortWidth[3] = { viewPortW.x, viewPortW.y, viewPortW.z };
    float pcfMargin = 0.02;
    
    [loop]
    for (int i = 0; i < 3; ++i)
    {
        float4 lightProj = mul(float4(posWorld, 1.0), shadowViewProj[i]);
        lightProj.xyz /= lightProj.w;
        
        if (lightProj.x < -1.0 + pcfMargin || lightProj.x > 1.0 - pcfMargin ||
            lightProj.y < -1.0 + pcfMargin || lightProj.y > 1.0 - pcfMargin ||
            lightProj.z < 0.0 + pcfMargin  || lightProj.z > 1.0 - pcfMargin)
        {
            continue;
        } 
        
        float bias = 0.001 + 0.01 * pow(1.0 - max(dot(lightDir, normal), 0.0), 3.0);
        float2 lightTexcoord = float2(lightProj.x * 0.5 + 0.5, lightProj.y * -0.5 + 0.5);
        
        float2 scaledTexcoord;
        scaledTexcoord.x = (lightTexcoord.x * (viewPortWidth[i] / width)) + (topLXOffsets[i] / width);
        scaledTexcoord.y = (lightTexcoord.y * (viewPortWidth[i] / height));
        
        float percentLit = 0.0;
        percentLit = shadowTex.SampleCmpLevelZero(shadowCompareSS, scaledTexcoord, lightProj.z - bias).r;
        
        float delta = 0.25 / viewPortWidth[i];
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            for (int x = -1; x <= 1; ++x)
            {
                percentLit += shadowTex.SampleCmpLevelZero(shadowCompareSS,
                                   scaledTexcoord.xy + float2(x * delta, y * delta), lightProj.z - bias).r;
            }
        }
        
        float shadowValue = percentLit / 10.0;
        return shadowValue + (1.0 - shadowValue) * (1.0 - saturate(radianceWeight / maxRadianceWeight));
    }
    
    return 1.0;
}

float3 getDirectLighting(float3 normal, float3 position, float3 albedo, float metallic, float roughness, bool useShadow)
{
    float3 pixelToEye = normalize(eyePos - position);
    float3 halfway = normalize(pixelToEye + lightDir);
    
    float NdotI = max(0.0, dot(normal, lightDir));
    float NdotH = max(0.0, dot(normal, halfway));
    float NdotO = max(0.0, dot(normal, pixelToEye));
    
    const float3 Fdielectric = 0.04; // 비금속(Dielectric) 재질의 F0
    float3 F0 = lerp(Fdielectric, albedo, metallic);
    float3 F = schlickFresnel(F0, max(0.0, dot(halfway, pixelToEye))); // HoV
    float D = ndfGGX(NdotH, roughness);
    float3 G = schlickGGX(NdotI, NdotO, roughness);
    float3 specularBRDF = (F * D * G) / max(1e-5, 4.0 * NdotI * NdotO);
    
    float3 kd = lerp(float3(1, 1, 1) - F, float3(0, 0, 0), metallic);
    float3 diffuseBRDF = kd * albedo;
    
    float shadowFactor = 1.0;
    if (useShadow)
        shadowFactor = getShadowFactor(position, normal);
    
    float3 radiance = radianceWeight * radianceColor * shadowFactor;
    
    return (diffuseBRDF + specularBRDF) * radiance * NdotI;
}

float3 getNormal(uint face)
{
    if (face == 0)
    {
        return float3(-1.0, 0.0, 0.0);
    }
    else if (face == 1)
    {
        return float3(1.0, 0.0, 0.0);
    }
    else if (face == 2)
    {
        return float3(0.0, -1.0, 0.0);
    }
    else if (face == 3)
    {
        return float3(0.0, 1.0, 0.0);
    }
    else if (face == 4)
    {
        return float3(0.0, 0.0, -1.0);
    }
    else
    {
        return float3(0.0, 0.0, 1.0);
    }
}

float2 getVoxelTexcoord(float3 pos, uint face)
{
    float2 texcoord = float2(0.0, 0.0);
    
    if (face == 0) // left
    {
        texcoord = float2(-pos.z + 32.0, -pos.y + 32.0);
    }
    else if (face == 1) // right
    {
        texcoord = float2(pos.z, -pos.y + 32.0);
    }
    else if (face == 2) // bottom
    {
        texcoord = float2(pos.x, pos.z);
    }
    else if (face == 3) // top
    {
        texcoord = float2(pos.x, -pos.z + 32.0);
    }
    else if (face == 4) // front
    {
        texcoord = float2(pos.x, -pos.y + 32.0);
    }
    else // back
    {
        texcoord = float2(-pos.x + 32.0, -pos.y + 32.0);
    }

    return texcoord;
}

#endif