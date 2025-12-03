#include "Common.hlsli"

Texture2DArray atlasTextureArray : register(t0);

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
    float alpha = atlasTextureArray.SampleLevel(pointWrapSS, float3(input.texcoord, (float)input.type), 0.0).a;
    if (alpha != 1.0)
        discard;
    
    // 스프라이트의 노멀벡터인 경우 보이는 쪽으로 설정
    float3 toEye = eyePos - input.posWorld;
    input.normal *= (dot(toEye, input.normal) < 0) ? -1 : 1;
    
    // must be [Normal * ITWorld * ITView]
    float3 normalView = mul(float4(input.normal, 0.0), view).xyz;
    return float4(normalView, 0.0);
}