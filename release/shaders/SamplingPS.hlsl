#include "Common.hlsli"

#ifdef MSAA_TEXTURE_SAMPLE
    Texture2DMS<float4, SAMPLE_COUNT> samplingTextureMS : register(t0);    
#else
    Texture2D samplingTexture : register(t0);
#endif


struct psInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

float4 main(psInput input) : SV_TARGET
{
#ifdef MSAA_TEXTURE_SAMPLE
    uint texWidth, texHeight, sampleCount;
    samplingTextureMS.GetDimensions(texWidth, texHeight, sampleCount);
    int2 texCoord = int2(input.texcoord * float2(texWidth, texHeight));
    
    float4 color = float4(0.0, 0.0, 0.0, 0.0);
    [unroll]
    for (int i = 0; i < SAMPLE_COUNT; ++i)
    {
        color += samplingTextureMS.Load(texCoord, i);
    }
    return color / SAMPLE_COUNT;
#else
    
    return samplingTexture.SampleLevel(linearWrapSS, input.texcoord, 0.0);
#endif
}

float4 mainGamma(psInput input) : SV_TARGET
{
    float4 color = float4(0.0, 0.0, 0.0, 0.0);
    
#ifdef MSAA_TEXTURE_SAMPLE
    uint texWidth, texHeight, sampleCount;
    samplingTextureMS.GetDimensions(texWidth, texHeight, sampleCount);
    int2 texCoord = int2(input.texcoord * float2(texWidth, texHeight));
    
    [unroll]
    for (int i = 0; i < SAMPLE_COUNT; ++i)
    {
        color += samplingTextureMS.Load(texCoord, i);
    }
    color /= SAMPLE_COUNT;
#else
    color = samplingTexture.SampleLevel(linearWrapSS, input.texcoord, 0.0);
#endif
    float4 invGamma = float4(1, 1, 1, 1) / 2.2;
    
    color = pow(color, invGamma);
    
    return color;

}