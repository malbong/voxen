#include "Common.hlsli"

Texture2D renderTex : register(t0);

struct psInput
{
    float4 posProj : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

float4 main(psInput input) : SV_TARGET
{
    float width, height, lod;
    renderTex.GetDimensions(0, width, height, lod);
    
    float dx = 1.0 / width; // src texel
    float dy = 1.0 / height; // src texel
    
    // a - b - c   ->  1  2  1
    // d - e - f   ->  2  4  2   -> 1/16
    // g - h - i   ->  1  2  1
    
    float3 a = renderTex.Sample(linearClampSS, float2(input.texcoord.x - dx, input.texcoord.y - dy)).rgb;
    float3 b = renderTex.Sample(linearClampSS, float2(input.texcoord.x,      input.texcoord.y - dy)).rgb;
    float3 c = renderTex.Sample(linearClampSS, float2(input.texcoord.x + dx, input.texcoord.y - dy)).rgb;
    
    float3 d = renderTex.Sample(linearClampSS, float2(input.texcoord.x - dx, input.texcoord.y)).rgb;
    float3 e = renderTex.Sample(linearClampSS, float2(input.texcoord.x     , input.texcoord.y)).rgb;
    float3 f = renderTex.Sample(linearClampSS, float2(input.texcoord.x + dx, input.texcoord.y)).rgb;
    
    float3 g = renderTex.Sample(linearClampSS, float2(input.texcoord.x - dx, input.texcoord.y + dy)).rgb;
    float3 h = renderTex.Sample(linearClampSS, float2(input.texcoord.x,      input.texcoord.y + dy)).rgb;
    float3 i = renderTex.Sample(linearClampSS, float2(input.texcoord.x + dx, input.texcoord.y + dy)).rgb;
    
    float3 color = float3(0.0, 0.0, 0.0);
    color += (a + c + g + i);
    color += (b + d + f + h) * 2.0;
    color += e * 4.0;
    color /= 16.0;
    
    return float4(color, 1.0);
}
