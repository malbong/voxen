#ifndef LIGHTING_HLSLI
    #define LIGHTING_HLSLI

#include "Common.hlsli"

Texture2D brdfTex : register(t10);
Texture2DArray shadowTex : register(t11);
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
        float3 T = float3(1.0, 0.0, 0.0);
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

float3 getBasicAmbientColor()
{
    float3 ambientColor = float3(1.0, 1.0, 1.0);
    
    float sunAltitude = lightDir.y;
    float dayAltitude = sin(PI / 24.0);
    if (sunAltitude <= dayAltitude)
    {
        float sunAniso = max(dot(lightDir, eyeDir), 0.0);
        float3 eyeHorizonColor = lerp(normalHorizonColor, sunHorizonColor, sunAniso);
        
        float maxHorizonColorAltitude = -sin(PI / 24.0);
        float altitudeWeight = smoothstep(maxHorizonColorAltitude, dayAltitude, sunAltitude);

        ambientColor = lerp(eyeHorizonColor, ambientColor, altitudeWeight);
    }
    
    float basicAmbientWeight = 0.5;
    
    return ambientColor * basicAmbientWeight;
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
    // - 그래서 기본색(getBasicAmbientColor)에 노멀방향에 대한 직접광 라이트색을 "더해서" 더 밝게 표현
    // - roughness는 사용되지 않음 -> 모든 방향에서 오는 빛을 모으는 과정이라 결국 모든 방향에서 더하면 거칠든 매끄럽든 동일하다는 가정
    float3 diffuseIrradiance = (radianceColor * max(dot(normal, lightDir), 0.0)) + getBasicAmbientColor();
    
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
    float3 basicAmbientColor = getBasicAmbientColor();
    float3 diffuseIrradiance = (radianceColor * max(dot(normal, lightDir), 0.0)) + basicAmbientColor;
    
    float3 reflectDir = normalize(reflect(-pixelToEye, normal));
    float3 reflectRadiance = useSkyColor ? getSkyColor(reflectDir) : basicAmbientColor;
    
    float3 specularIrradiance = lerp(reflectRadiance, diffuseIrradiance, roughness);
    
    float3 Fdielectric = float3(0.04, 0.04, 0.04);
    float3 F0 = lerp(Fdielectric, albedo, metallic);
    float2 specularBRDF = brdfTex.Sample(pointClampSS, float2(dot(pixelToEye, normal), 1 - roughness)).rg;
    
    return specularIrradiance * (specularBRDF.r * F0 + specularBRDF.g);
}

float3 getAmbientLighting(float ao, float3 albedo, float3 position, float3 normal, float metallic, float roughness, bool useSkyColor)
{
    float3 pixelToEye = normalize(eyePos - position);
    
    float3 diffuseTerm = getDiffuseTerm(albedo, pixelToEye, normal, metallic) * 0.5;
    float3 specularTerm = getSpecularTerm(albedo, pixelToEye, normal, metallic, roughness, useSkyColor);
    
    return ao * (diffuseTerm + specularTerm);
}

bool sampleCascade(float3 posWorld, uint cascadeIndex, float NdotL, out float outPercentLit)
{
    const float baseBias[3] = { 0.0025, 0.002, 0.002 };
    const float slopeBias[3] = { 0.005, 0.005, 0.002 };
    
    float bias = baseBias[cascadeIndex] + slopeBias[cascadeIndex] * pow(1.0 - NdotL, 3.0);
    
    float4 lightProj = mul(float4(posWorld, 1.0), shadowViewProj[cascadeIndex]);
    lightProj.xyz /= lightProj.w;
    
    /*
    * lightProj.xy는 단순히 NDC [-1, 1] 범위 검사
    * - 끝쪽은 다음 cascade를 검사하도록 진행
    *
    * lightProj.z는 bias를 이용한 NDC [0 + bias, 1] 범위 검사
    * - lightProj.z가 bias보다 작아지는 경우를 방지하기 위함
    *
    * lightProj.z - bias가 음수가 되는 경우 다음과 같은 문제 발생
    * - 실제 깊이(ligtingProj.z)가 샘플링된 곳보다 깊어 그림자가 생겨야하나, -bias로 인해 음수로 그림자가 생기지 않음
    * - Peter Panning 현상과 유사하나, 단순히 bias가 커서 그림자가 안생기는 문제가 아닌, 음수 자체를 비교하는게 문제
    * - DepthClipEnable = false로 동작하는 ShadowMap RS 특성상 카메라 뒤의 물체의 Depth가 clamp되어 0.0으로 기록
    * - 이 때, lightProj.z값이 0.0에 수렴하는 값인 경우 문제가 발생할 것
    */
    if (-1.0 < lightProj.x && lightProj.x < 1.0 &&
        -1.0 < lightProj.y && lightProj.y < 1.0 &&
         0.0 < lightProj.z - bias && lightProj.z < 1.0)
    {
        float2 lightTexcoord = float2(lightProj.x * 0.5 + 0.5, -(lightProj.y * 0.5) + 0.5);
    
        outPercentLit = shadowTex.SampleCmpLevelZero(shadowCompareSS, float3(lightTexcoord, cascadeIndex),
                                                                        lightProj.z - bias, 0.0).r;
        
        return true;
    }
    
    outPercentLit = 1.0;
    return false;
}

float getShadowFactor(float3 posWorld, float3 normal, out uint outCascadeIndex)
{
    outCascadeIndex = cascadeLevel;
    
    float NdotL = max(dot(lightDir, normal), 0.0);
    
    float blendRange = 0.25;
    float viewDistZ = dot(posWorld - eyePos, eyeDir); // viewZ: |posWorld-eyePos| * |eyeDir| * cosTheta
    
    float cascadeDistance[4] = { cascadeSplits.x, cascadeSplits.y, cascadeSplits.z, cascadeSplits.w };
    
    // 거리 체크
    uint cascadeIndex = cascadeLevel; // 3
    [unroll]
    for (uint i = 0; i < cascadeLevel; ++i)
    {
        if (cascadeDistance[i] <= viewDistZ && viewDistZ < cascadeDistance[i + 1])
        {
            cascadeIndex = i;
            break;
        }
    }
    
    // 거리에 대한 유효성 검사
    // 거리를 벗어나면 유효하지 않음 -> 그림자 없음
    if (cascadeIndex == cascadeLevel)
        return 1.0;
    
    // 샘플링 시도 -> 실패 시 다음 cascade로 이동 (예외적)
    float percentLit;
    if (!sampleCascade(posWorld, cascadeIndex, NdotL, percentLit))
    {
        cascadeIndex = min(cascadeIndex + 1, cascadeLevel - 1);
        if (!sampleCascade(posWorld, cascadeIndex, NdotL, percentLit))
            return 1.0;
    }
    
    outCascadeIndex = cascadeIndex;
    
    if (useCascadeBlend && cascadeIndex + 1 < cascadeLevel)
    {
        const float bandWidth = 0.4;
        float nearZ = cascadeDistance[cascadeIndex];
        float farZ = cascadeDistance[cascadeIndex + 1];
        float blendStartZ = lerp(farZ, nearZ, bandWidth);
        
        // viewDistZ가 blendStartZ 보다 작거나 같은 경우 blending을 하지 않게 됨
        float blendWeight = smoothstep(blendStartZ, farZ, viewDistZ);
        if (blendWeight > 0.0)
        {
            float nextPercentLit;
            if (sampleCascade(posWorld, cascadeIndex + 1, NdotL, nextPercentLit))
            {
                percentLit = lerp(percentLit, nextPercentLit, blendWeight);
                outCascadeIndex = cascadeLevel + 1;
            }
        }
    }
    
    
    // 해가 강할 수록 shadow를 진하게 표현
    float radianceShadowWeight = clamp(radianceWeight / maxRadianceWeight, 0.0, 1.0);
    float radianceShadow = lerp(percentLit, 1.0, 1.0 - radianceShadowWeight);
    return radianceShadow;
    
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
    
    uint cascadeIndex = 0;
    float shadowFactor = useShadow ? getShadowFactor(position, normal, cascadeIndex) : 1.0;
    shadowFactor = pow(shadowFactor, 3.0);

    float3 radiance = radianceWeight * radianceColor * shadowFactor;

    float3 directLightingColor = (diffuseBRDF + specularBRDF) * radiance * NdotI;
    
    if (useCascadeColor)
    {
        float3 cascadeColor = float3(1, 1, 1);
        if (cascadeIndex == 0)
            cascadeColor = float3(1, 0, 0);
        else if (cascadeIndex == 1)
            cascadeColor = float3(0, 1, 0);
        else if (cascadeIndex == 2)
            cascadeColor = float3(0, 0, 1);
        else if (cascadeIndex == cascadeLevel + 1) // blending
            cascadeColor = float3(1, 1, 0);
        else
            cascadeColor = float3(0.25, 0.25, 0.25);
        
        directLightingColor *= 0.5f;
        directLightingColor += cascadeColor * 0.5;
    }
    
    return directLightingColor;
}

#endif