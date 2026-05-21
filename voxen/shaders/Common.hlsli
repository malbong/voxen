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
    bool useFullSemiAlphaEdge;
    bool useSSAO;
    float2 appDummy;
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

#endif