#include "Common.hlsli"

Texture2D sunTexture : register(t0);
Texture2D moonTexture : register(t1);

struct psInput
{
    float4 posProj : SV_POSITION;
    float3 posWorld : POSITION;
};

bool getPlanetTexcoord(float3 posDir, float3 planetDir, float size, out float2 texcoord)
{
    texcoord = float2(0.0, 0.0);
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
        
        texcoord.x = 0.5 + vTBN.x * (0.5 / size);
        texcoord.y = 0.5 + vTBN.y * (0.5 / size);
        ret = true;
    }
    
    return ret;
}

float4 main(psInput input) : SV_TARGET
{
    float3 color = float3(0.0, 0.0, 0.0);
    float3 posDir = normalize(input.posWorld);
#ifdef USE_MIRROR
    posDir.y *= -1;
#endif
    
    float sunAltitude = lightDir.y;
    float showSectionAltitude = -PI * 0.5 * (1.7 / 6.0);
    
    // sun
    float sunSize = lerp(0.25, 0.6, pow(max(dot(lightDir, eyeDir), 0.0), 3.0));
    float2 sunTexcoord;
    if (sunAltitude > showSectionAltitude && getPlanetTexcoord(posDir, lightDir, sunSize, sunTexcoord))
    {
#ifdef USE_MIRROR
        color += sunTexture.SampleLevel(linearClampSS, sunTexcoord, 0.0).rgb * radianceWeight;
#else
        color += sunTexture.SampleLevel(pointWrapSS, sunTexcoord, 0.0).rgb * radianceWeight;
#endif
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
#ifdef USE_MIRROR
        color += moonTexture.SampleLevel(linearClampSS, moonTexcoord, 0.0).rgb * moonRadianceWeight;
#else
        color += moonTexture.SampleLevel(pointWrapSS, moonTexcoord, 0.0).rgb * moonRadianceWeight;
#endif
    }
    
    // background sky
    float sunDirWeight = 0.0;
    if (sunAltitude > showSectionAltitude)
        sunDirWeight = max(dot(lightDir, eyeDir), 0.0);
    float posAltitude = sin(posDir.y);
   
    float3 horizonColor = lerp(normalHorizonColor, sunHorizonColor, sunDirWeight);
    float3 zenithColor = lerp(normalZenithColor, sunZenithColor, sunDirWeight);
    
    // zenith와 horizon 구별 고도 고려
    // 최대한 구별된 색 선택하도록 결정
    float3 mixColor = (horizonColor + zenithColor) * 0.5;
    float horizonAltitude = PI / 24.0;
    
    if (posAltitude <= horizonAltitude)
        color += lerp(horizonColor, mixColor, pow((posAltitude + 1.0) / (1.0 + horizonAltitude), 15.0));
    else
        color += lerp(mixColor, zenithColor, pow((posAltitude - horizonAltitude) / (1.0 - horizonAltitude), 0.5));
    
    return float4(color, 1.0);
}


