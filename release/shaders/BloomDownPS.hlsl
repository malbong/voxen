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
    
    // a - b - c    (j k l m) / 4 * 0.5
    // - j - k -    (a b d e) / 4 * 0.125
    // d - e - f    (b c e f) / 4 * 0.125
    // - l - m -    (d e g h) / 4 * 0.125
    // g - h - i    (e f h i) / 4 * 0.125
    
    float3 a = renderTex.Sample(linearClampSS, float2(input.texcoord.x - 2 * dx, input.texcoord.y - 2 * dy)).rgb;
    float3 b = renderTex.Sample(linearClampSS, float2(input.texcoord.x, input.texcoord.y - 2 * dy)).rgb;
    float3 c = renderTex.Sample(linearClampSS, float2(input.texcoord.x + 2 * dx, input.texcoord.y - 2 * dy)).rgb;

    float3 d = renderTex.Sample(linearClampSS, float2(input.texcoord.x - 2 * dx, input.texcoord.y)).rgb;
    float3 e = renderTex.Sample(linearClampSS, float2(input.texcoord.x, input.texcoord.y)).rgb;
    float3 f = renderTex.Sample(linearClampSS, float2(input.texcoord.x + 2 * dx, input.texcoord.y)).rgb;

    float3 g = renderTex.Sample(linearClampSS, float2(input.texcoord.x - 2 * dx, input.texcoord.y + 2 * dy)).rgb;
    float3 h = renderTex.Sample(linearClampSS, float2(input.texcoord.x, input.texcoord.y + 2 * dy)).rgb;
    float3 i = renderTex.Sample(linearClampSS, float2(input.texcoord.x + 2 * dx, input.texcoord.y + 2 * dy)).rgb;

    float3 j = renderTex.Sample(linearClampSS, float2(input.texcoord.x - dx, input.texcoord.y - dy)).rgb;
    float3 k = renderTex.Sample(linearClampSS, float2(input.texcoord.x + dx, input.texcoord.y - dy)).rgb;
    float3 l = renderTex.Sample(linearClampSS, float2(input.texcoord.x - dx, input.texcoord.y + dy)).rgb;
    float3 m = renderTex.Sample(linearClampSS, float2(input.texcoord.x + dx, input.texcoord.y + dy)).rgb;
    
    float3 color = float3(0.0, 0.0, 0.0);
    
    color += (j + k + l + m) * 0.25 * 0.5;
    color += (a + b + d + e) * 0.25 * 0.125;
    color += (b + c + e + f) * 0.25 * 0.125;
    color += (d + e + g + h) * 0.25 * 0.125;
    color += (e + f + h + i) * 0.25 * 0.125;
    
    return float4(color, 1.0);
}
