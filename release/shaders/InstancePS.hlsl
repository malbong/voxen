#include "Common.hlsli"

Texture2DArray atlasTextureArray : register(t0);
Texture2D grassColorMap : register(t1);

#ifdef USE_DEPTH_CLIP
    Texture2D depthTex : register(t2);
#endif

struct vsOutput
{
    float4 posProj : SV_POSITION;
    float3 posWorld : POSITION;
    float3 normal : NORMAL;
    sample float2 texcoord : TEXCOORD;
    uint type : TYPE;
};

float4 main(vsOutput input) : SV_TARGET
{
    float alpha = atlasTextureArray.SampleLevel(pointWrapSS, float3(input.texcoord, (float) input.type), 0.0).a;
    if (alpha != 1.0)
        discard;
    
#ifdef USE_DEPTH_CLIP
    float width, height, lod;
    depthTex.GetDimensions(0, width, height, lod);
    
    float2 screenTexcoord = float2(input.posProj.x / width, input.posProj.y / height);
    float planeDepth = depthTex.Sample(linearClampSS, screenTexcoord).r;
    float pixelDepth = input.posProj.z;

    if (pixelDepth < planeDepth)
    {
        discard;
    }
    
    float4 color = atlasTextureArray.Sample(linearWrapSS, float3(input.texcoord, (float) input.type));
#else
    float4 color = atlasTextureArray.SampleLevel(pointWrapSS, float3(input.texcoord, (float) input.type), 0.0);
#endif
  
    return color;
}