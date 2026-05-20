#ifndef COMMON_HLSLI
#define COMMON_HLSLI

#define PI 3.14159265
#define SAMPLE_COUNT 4

#define CHUNK_SIZE 32
#define CHUNK_COUNT 21

#define LEFT 0
#define RIGHT 1
#define BOTTOM 2
#define TOP 3
#define NEAR 4
#define FAR 5

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
    float4 imGUIData;
}

cbuffer CameraConstantBuffer : register(b8)
{
    Matrix view;
    Matrix proj;
    Matrix invView;
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

// https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
// F, G, D ???
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
    
    // ??? PBR ?????? ???
    // float3 diffuseIrradiance = irradianceIBLTex.Sample(linearSampler, normalWorld);
    // ?? ???? ????
    // float3 diffuseIrradiance = (radianceColor * max(dot(normal, lightDir), 0.0)) + getAmbientColor();
    // - diffuseIBL?? ??? ?????? ???? ???????? ???? diffuse?? ??? ?? ??
    // - ???????? ???? ??????? ??? ?????? ????? ????? ??????? ??? ?? ??? ?? ????
    // - ????? ????(getAmbientColor)?? ?????? ???? ????????? "?????" ?? ??? ???
    // - roughness?? ?????? ???? -> ??? ?????? ???? ???? ?????? ??????? ?? ??? ?????? ????? ????? ??????? ???????? ????
    float3 diffuseIrradiance = (radianceColor * max(dot(normal, lightDir), 0.0)) + getAmbientColor();
    
    return kd * albedo * diffuseIrradiance;
}

float3 getSpecularTerm(float3 albedo, float3 pixelToEye, float3 normal, float metallic, float roughness)
{
    float2 specularBRDF = brdfTex.Sample(pointClampSS, float2(dot(pixelToEye, normal), 1 - roughness)).rg;
    
    // ??? PBR ?????? ???
    // float3 specularIrradiance = specularIBLTex.SampleLevel(linearSampler, reflect(-pixelToEye, normal), roughness * 5.0f).rgb;
    // ?? ???? ????
    // float3 specularIrradiance = lerp(reflectRadiance, ambientColor, roughness);
    // - ??? ?????? ???? ???????? ???? specular?? ??? ?? ??
    // - ?????? ???? ??? ???? ???? ???©ª???? ????? ??????? ?? ????? ?????
    // - ??, ????? ?????? ??? ?????? ????? ?????? ???? ???©ª?, ???? ????? ??? ??????? ???©ª?
    //   ->lerp(radianceColor, getAmbientColor(), roughness)
    float3 ambientColor = getAmbientColor();
    float3 reflectDir = normalize(reflect(-pixelToEye, normal));
    float reflectRadianceWeight = max(dot(reflectDir, lightDir), 0.0);
    float3 reflectRadiance = lerp(ambientColor, radianceColor, reflectRadianceWeight);
    float3 specularIrradiance = reflectRadiance;
    
    float3 Fdielectric = float3(0.04, 0.04, 0.04);
    float3 F0 = lerp(Fdielectric, albedo, metallic);

    return specularIrradiance * (specularBRDF.r * F0 + specularBRDF.g);
}

float3 getAmbientLighting(float ao, float3 albedo, float3 position, float3 normal, float metallic, float roughness)
{  
    float3 pixelToEye = normalize(eyePos - position);
    
    float3 diffuseTerm = getDiffuseTerm(albedo, pixelToEye, normal, metallic);
    float3 specularTerm = getSpecularTerm(albedo, pixelToEye, normal, metallic, roughness);
    
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
    
    const float3 Fdielectric = 0.04;
    float3 F0 = lerp(Fdielectric, albedo, metallic);
    float3 F = schlickFresnel(F0, max(0.0, dot(halfway, pixelToEye))); // HoV
    float D = ndfGGX(NdotH, roughness);
    float3 G = schlickGGX(NdotI, NdotO, roughness);
    float3 specularBRDF = (F * D * G) / max(1e-5, 4.0 * NdotI * NdotO);
    
    float3 kd = lerp(float3(1, 1, 1) - F, float3(0, 0, 0), metallic);
    float3 diffuseBRDF = kd * albedo;
    
    float shadowFactor = useShadow ? getShadowFactor(position, normal) : 1.0;
    shadowFactor = pow(shadowFactor, 3.0);
    
    float3 radiance = radianceWeight * radianceColor * shadowFactor;
    
    return (diffuseBRDF + specularBRDF) * radiance * NdotI;
}

#endif