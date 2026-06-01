#ifndef LIGHTING_HLSLI
    #define LIGHTING_HLSLI

#include "Common.hlsli"
#include "Shadow.hlsli"

Texture2D brdfTex : register(t10);
Texture2D sunTex : register(t11);
Texture2D moonTex : register(t12);

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

        float3 vTBN = mul(p, transpose(TBNMatrix));

        outTexcoord.x = (vTBN.x / size + 1.0) * 0.5;
        outTexcoord.y = (vTBN.y / size + 1.0) * 0.5;
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

        uint index = days % 8;
        uint2 indexUV = uint2(index % col, index / col);

        moonTexcoord += indexUV;
        moonTexcoord = float2(moonTexcoord.x / col, moonTexcoord.y / row);

        float moonRadianceWeight = (maxRadianceWeight - radianceWeight) / maxRadianceWeight;
        skyColor += moonTex.SampleLevel(pointWrapSS, moonTexcoord, 0.0).rgb * moonRadianceWeight;
    }

    // background sky
    float sunDirWeight = 0.0;
    if (sunAltitude > showSectionAltitude)
        sunDirWeight = max(dot(lightDir, eyeDir), 0.0);

    float3 horizonColor = lerp(normalHorizonColor, sunHorizonColor, sunDirWeight);
    float3 zenithColor = lerp(normalZenithColor, sunZenithColor, sunDirWeight);

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

    float3 diffuseIrradiance = (radianceColor * max(dot(normal, lightDir), 0.0)) + getBasicAmbientColor();

    return kd * albedo * diffuseIrradiance;
}

float3 getSpecularTerm(float3 albedo, float3 pixelToEye, float3 normal, float metallic, float roughness, bool useSkyColor)
{
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

float3 getDirectLighting(float3 normal, float3 position, float3 albedo, float metallic, float roughness, bool useShadow)
{
    float3 pixelToEye = normalize(eyePos - position);
    float3 halfway = normalize(pixelToEye + lightDir);

    float NdotI = max(0.0, dot(normal, lightDir));
    float NdotH = max(0.0, dot(normal, halfway));
    float NdotO = max(0.0, dot(normal, pixelToEye));

    const float3 Fdielectric = 0.04;
    float3 F0 = lerp(Fdielectric, albedo, metallic);
    float3 F = schlickFresnel(F0, max(0.0, dot(halfway, pixelToEye)));
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
        float3 cascadeColor = getCascadeColor(cascadeIndex);
        
        directLightingColor *= 0.5f;
        directLightingColor += cascadeColor * 0.5;
    }

    return directLightingColor;
}

#endif
