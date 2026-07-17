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

cbuffer AppConstantBuffer : register(b7)
{
    float appWidth;
    float appHeight;
    float mirrorWidth;
    float mirrorHeight;
}

cbuffer RenderStatesConstantBuffer : register(b8)
{
    bool useFullSemiAlphaEdge;
    bool useSSAO;
    bool useCascadeColor;
    bool useCascadeBlend;
    bool useMapBasedCascade;
    bool useBloom;
    bool toggleTonemapping;
    uint toneMappingFunctionIndex;
}

cbuffer CameraConstantBuffer : register(b9)
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

cbuffer SkyboxConstantBuffer : register(b10)
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

cbuffer LightConstantBuffer : register(b11)
{
    float3 lightDir;
    float radianceWeight;
    float3 radianceColor;
    float maxRadianceWeight;
}

cbuffer ShadowConstantBuffer : register(b12)
{
    Matrix shadowViewProj[3];
    float4 cascadeSplits;
    uint cascadeWidth;
    uint cascadeHeight;
    uint cascadeLevel;
    uint shadowDummy;
}

cbuffer DateConstantBuffer : register(b13)
{
    uint days;
    uint dateTime;
    uint dayCycleRealTime;
    uint dayCycleAmount;
}

#endif