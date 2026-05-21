#ifndef LIGHTING_HLSLI
    #define LIGHTING_HLSLI

#include "Common.hlsli"

Texture2D brdfTex : register(t10);
Texture2D shadowTex : register(t11);
Texture2D sunTex : register(t12);
Texture2D moonTex : register(t13);

// https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
float3 schlickFresnel(float3 F0, float VdotH)
{
    return F0 + (1 - F0) * pow(2.0, (-5.55473 * (VdotH) - 6.98316) * VdotH);
}

float ndfGGX(float NdotH, float roughness)
{
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    return alpha2 / max(1e-5, (PI * pow(NdotH * NdotH * (alpha2 - 1) + 1, 2.0)));
}

float schlickGGX(float NdotI, float NdotO, float roughness)
{
    float k = (roughness + 1) * (roughness + 1) / 8;
    float gl = NdotI / (NdotI * (1 - k) + k);
    float gv = NdotO / (NdotO * (1 - k) + k);
    return gl * gv;
}

bool getPlanetTexcoord(float3 posDir, float3 planetDir, float size, out float2 outTexcoord)
{
    outTexcoord = float2(0.0, 0.0);
    bool ret = false;
    
    float PDotP = dot(planetDir, posDir);
    float3 v = normalize(posDir - PDotP * planetDir);
    float3 p = v * tan(acos(PDotP));
    
    if (PDotP > 0.0 && length(p) < size)
    {
        float3 N = -planetDir;
        float3 T = float3(cos(PI / 4.0), 0.0, -cos(PI / 4.0));
        float3 B = cross(N, T);
        float3x3 TBNMatrix = float3x3(T, B, N);
        
        // TBN좌표 * 월드좌표기준정의된TBN직교행렬 -> 월드좌표
        // 월드좌표 * 월드좌표기준정의된TBN직교행렬의역행렬 -> TBN좌표
        float3 vTBN = mul(p, transpose(TBNMatrix)); // 직교 행렬의 역행렬은 전치행렬
        
        outTexcoord.x = (vTBN.x / size + 1.0) * 0.5; // vTBN.x => [-size, size]
        outTexcoord.y = (vTBN.y / size + 1.0) * 0.5; // vTBN.x => [-size, size]
        ret = true;
    }
    
    return ret;
}

float3 getSkyColor(float3 posDir)
{
    float3 skyColor = float3(0.0, 0.0, 0.0);
    
    float sunAltitude = lightDir.y;
    float showSectionAltitude = -sin(PI * 0.25);
        
    // sun
    float sunSize = lerp(0.25, 0.6, pow(max(dot(lightDir, eyeDir), 0.0), 3.0));
    float2 sunTexcoord;
    if (sunAltitude > showSectionAltitude && getPlanetTexcoord(posDir, lightDir, sunSize, sunTexcoord))
    {
        skyColor += sunTex.SampleLevel(pointWrapSS, sunTexcoord, 0.0).rgb * radianceWeight;
    }
    
    // moon
    float moonSize = lerp(0.125, 0.3, pow(max(dot(-lightDir, eyeDir), 0.0), 3.0));
    float2 moonTexcoord;
    if (-sunAltitude > showSectionAltitude && getPlanetTexcoord(posDir, -lightDir, moonSize, moonTexcoord))
    {
        uint col = 4;
        uint row = 2;
        
        uint index = days % 8; // 0 ~ 7
        uint2 indexUV = uint2(index % col, index / col); // [0,0]~[3,1]
        
        moonTexcoord += indexUV; // moonTexcoord : [0,0]~[4,2] 
        moonTexcoord = float2(moonTexcoord.x / col, moonTexcoord.y / row); // [4,2]->[1,1]
        
        float moonRadianceWeight = (maxRadianceWeight - radianceWeight) / maxRadianceWeight;
        skyColor += moonTex.SampleLevel(pointWrapSS, moonTexcoord, 0.0).rgb * moonRadianceWeight;
    }
    
    // background sky
    float sunDirWeight = 0.0;
    if (sunAltitude > showSectionAltitude)
        sunDirWeight = max(dot(lightDir, eyeDir), 0.0);
   
    float3 horizonColor = lerp(normalHorizonColor, sunHorizonColor, sunDirWeight);
    float3 zenithColor = lerp(normalZenithColor, sunZenithColor, sunDirWeight);
    
    // zenith와 horizon 구별 고도 고려
    // 최대한 구별된 색 선택하도록 결정
    float3 mixColor = (horizonColor + zenithColor) * 0.5;
    float horizonAltitude = sin(PI / 24.0);
    float posAltitude = posDir.y;
        
    if (posAltitude <= horizonAltitude)
        skyColor += lerp(horizonColor, mixColor, pow(abs(posAltitude + 1.0) / (1.0 + horizonAltitude), 15.0));
    else
        skyColor += lerp(mixColor, zenithColor, pow(abs(posAltitude - horizonAltitude) / (1.0 - horizonAltitude), 0.5));
    
    return skyColor;
}

float3 getAmbientColor()
{
    float3 ambientColor = float3(1.0, 1.0, 1.0);
    
    float sunAltitude = lightDir.y;
    float dayAltitude = sin(PI / 24.0);
    if (sunAltitude <= dayAltitude)
    {
        float maxHorizonColorAltitude = -sin(PI / 24.0);
        
        float sunAniso = max(dot(lightDir, eyeDir), 0.0);
        float3 eyeHorizonColor = lerp(normalHorizonColor, sunHorizonColor, sunAniso);
        
        float w = smoothstep(maxHorizonColorAltitude, dayAltitude,
                                clamp(sunAltitude, maxHorizonColorAltitude, dayAltitude));
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

float3 getSpecularTerm(float3 albedo, float3 pixelToEye, float3 normal, float metallic, float roughness, bool useSkyColor)
{
    // 언리얼 PBR 코스노트 코드
    // float3 specularIrradiance = specularIBLTex.SampleLevel(linearSampler, reflect(-pixelToEye, normal), roughness * 5.0f).rgb;
    // 내 코드와 이유
    // 정적인 환경맵을 IBLBacker로 구워서 specularIBLTex로 사용할 수 없는 환경임
    // 이러한 이유로 specularIBLTex를 근사하여 만듦
    // roughness가 높으면 DiffuseIrradiance와 유사하게 구성하고, roughness가 낮으면 반사방향의 skyColor를 가져오는 방식을 취함
    float3 ambientColor = getAmbientColor();
    float3 diffuseIrradiance = (radianceColor * max(dot(normal, lightDir), 0.0)) + ambientColor;
    
    float3 reflectDir = normalize(reflect(-pixelToEye, normal));
    float3 reflectionColor = useSkyColor ? getSkyColor(reflectDir) : ambientColor;
    float reflectRadianceWeight = max(dot(reflectDir, lightDir), 0.0);
    float3 reflectRadiance = reflectionColor * reflectRadianceWeight;
    
    float3 specularIrradiance = lerp(reflectRadiance, diffuseIrradiance, roughness);
    
    float3 Fdielectric = float3(0.04, 0.04, 0.04);
    float3 F0 = lerp(Fdielectric, albedo, metallic);
    float2 specularBRDF = brdfTex.Sample(pointClampSS, float2(dot(pixelToEye, normal), 1 - roughness)).rg;
    
    return specularIrradiance * (specularBRDF.r * F0 + specularBRDF.g);
}

float3 getAmbientLighting(float ao, float3 albedo, float3 position, float3 normal, float metallic, float roughness, bool useSkyColor)
{
    float3 pixelToEye = normalize(eyePos - position);
    
    float3 diffuseTerm = getDiffuseTerm(albedo, pixelToEye, normal, metallic);
    float3 specularTerm = getSpecularTerm(albedo, pixelToEye, normal, metallic, roughness, useSkyColor);
    
    float weight = 0.5;
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
            lightProj.z < 0.0 + pcfMargin || lightProj.z > 1.0 - pcfMargin)
        {
            continue;
        }
        
        float bias = 0.002 + 0.01 * pow(1.0 - max(dot(lightDir, normal), 0.0), 3.0);
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
    
    const float3 Fdielectric = 0.04;
    float3 F0 = lerp(Fdielectric, albedo, metallic);
    float3 F = schlickFresnel(F0, max(0.0, dot(halfway, pixelToEye))); // HoV
    float D = ndfGGX(NdotH, roughness);
    float3 G = schlickGGX(NdotI, NdotO, roughness);
    float3 specularBRDF = (F * D * G) / max(1e-4, 4.0 * NdotI * NdotO);
    
    float3 kd = lerp(float3(1, 1, 1) - F, float3(0, 0, 0), metallic);
    float3 diffuseBRDF = kd * albedo;
    
    float shadowFactor = useShadow ? getShadowFactor(position, normal) : 1.0;
    shadowFactor = pow(shadowFactor, 3.0);
    
    float3 radiance = radianceWeight * radianceColor * shadowFactor;
    
    return (diffuseBRDF + specularBRDF) * radiance * NdotI;
}

#endif