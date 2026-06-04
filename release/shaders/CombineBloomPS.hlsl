#include "Common.hlsli"

#define LINEAR                              1
#define REINHARD                            2
#define LUMA_BASED_REINHARD                 3
#define WHITE_PRESERV_REINHARD              4
#define LUMA_BASED_WHITE_PRESERV_REINHARD   5
#define FILMIC                              6
#define UNCHARTED2                          7

Texture2D renderTex : register(t0);
Texture2D bloomTex : register(t1);

struct psInput
{
    float4 posProj : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

float henyeyGreensteinPhase(float3 L, float3 V, float aniso)
{
	// L: toLight
	// V: eyeDir
	// https://www.shadertoy.com/view/7s3SRH
    float cosT = dot(L, V);
    float g = aniso;
    return (1.0 - g * g) / (4.0 * PI * pow(abs(1.0 + g * g - 2.0 * g * cosT), 3.0 / 2.0));
}

float3 gammaCorrection(float3 color, float gamma)
{
    float3 invGamma = float3(1, 1, 1) / gamma;
    
    return pow(color, invGamma);
}

float3 linearToneMapping(float3 color)
{
    color = clamp(color, 0.0, 1.0);

    return gammaCorrection(color, 2.2);
}

float3 simpleReinhardToneMapping(float3 color)
{
    color = color / (1.0 + color);
    
    return gammaCorrection(color, 2.2);
}

float3 whitePreservingReinhardToneMapping(float3 color)
{
    float white = 2.0;
    float white2 = white * white;
    
    color = color * (1.0 + color / white2) / (1.0 + color);

    return gammaCorrection(color, 2.2);
}

float3 lumaBasedReinhardToneMapping(float3 color)
{
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    
    float toneMappedLuma = luma / (1.0 + luma);
    color *= toneMappedLuma / luma;
    
    return gammaCorrection(color, 2.2);
}

float3 whitePreservingLumaBasedReinhardToneMapping(float3 color)
{
    float white = 2.0;
    float white2 = white * white;
    
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    
    float toneMappedLuma = luma * (1.0 + luma / white2) / (1.0 + luma);
    color *= toneMappedLuma / luma;
    
    return gammaCorrection(color, 2.2);
}

float3 filmicToneMapping(float3 color)
{
    color = max(float3(0, 0, 0), color);
    color = (color * (6.2 * color + 0.5)) / (color * (6.2 * color + 1.7) + 0.06);
    
    return color;
}

float3 uncharted2ToneMapping(float3 color)
{
    float3 invGamma = float3(1, 1, 1) / 2.2;
    
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;
    float W = 11.2;
    
    color = ((color * (A * color + C * B) + D * E) / (color * (A * color + B) + D * F)) - E / F;
    float white = ((W * (A * W + C * B) + D * E) / (W * (A * W + B) + D * F)) - E / F;
    color /= white;
    
    return gammaCorrection(color, 2.2);
}

float4 main(psInput input) : SV_TARGET
{
    float3 renderColor = renderTex.Sample(pointClampSS, input.texcoord).rgb;
    float3 bloomColor = bloomTex.Sample(pointClampSS, input.texcoord).rgb;
    
    float scattering = min(henyeyGreensteinPhase(lightDir, eyeDir, 0.9), 1.0) * 0.3;
    float bloomStrength = scattering + (isUnderWater ? 0.125 : 0);
    bloomStrength *= (useBloom ? 1.0 : 0.0);
    
    float3 combineColor = lerp(renderColor, bloomColor, bloomStrength);
    
    float exposure = 1.5;
    combineColor *= exposure;
    
    /*
    * Tone Mapping Sample
    * https://www.shadertoy.com/view/lslGzl
    */
    switch (toneMappingFunctionIndex)
    {
        case LINEAR:
            combineColor = linearToneMapping(combineColor);
            break;
        case REINHARD:
            combineColor = simpleReinhardToneMapping(combineColor);
            break;
        case LUMA_BASED_REINHARD:
            combineColor = lumaBasedReinhardToneMapping(combineColor);
            break;
        case WHITE_PRESERV_REINHARD:
            combineColor = whitePreservingReinhardToneMapping(combineColor);
            break;
        case LUMA_BASED_WHITE_PRESERV_REINHARD:
            combineColor = whitePreservingLumaBasedReinhardToneMapping(combineColor);
            break;
        case FILMIC:
            combineColor = filmicToneMapping(combineColor);
            break;
        case UNCHARTED2:
            combineColor = uncharted2ToneMapping(combineColor);
            break;
        default:
            combineColor = whitePreservingLumaBasedReinhardToneMapping(combineColor);
            break;
    }
    
    return float4(combineColor, 1.0);
}