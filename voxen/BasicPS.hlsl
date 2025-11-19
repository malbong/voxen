#include "Common.hlsli"

Texture2DArray blockAtlasTextureArray : register(t0);
Texture2DArray normalAtlasTextureArray : register(t1);
Texture2DArray merAtlasTextureArray : register(t2);
Texture2D grassColorMap : register(t3);
Texture2D foliageColorMap : register(t4);
Texture2D climateNoiseMap : register(t5);
Texture2D mirrorDepthTex : register(t6);

struct psInput
{
    float4 posProj : SV_POSITION;
    sample float3 posWorld : POSITION;
    sample float3 normal : NORMAL;
    sample float2 texcoord : TEXCOORD;
    uint texIndex : INDEX;
};

struct psOutput
{
    float4 normalEdge : SV_Target0;
    float4 position : SV_Target1;
    float4 albedo : SV_Target2;
    uint coverage : SV_Target3;
    float4 mer : SV_Target4;
};

bool useDirtOverlay(uint texIndex)
{
    return texIndex == 2;
}

bool useGrassColor(uint texIndex)
{
    // TODO : 특이한 TEXTURE 정리 -> grass, foliage, side overlay
    return texIndex <= 2 || texIndex == 128 || texIndex == 131 || texIndex == 148;
}

bool useFoliageColor(uint texIndex)
{
    // TODO
    return (64 <= texIndex && texIndex <= 69);
}

float4 getAlbedo(float2 texcoord, uint texIndex, float3 worldPos, float3 normal)
{
    float4 albedo = blockAtlasTextureArray.Sample(pointWrapSS, float3(texcoord, texIndex));
    
    if (useGrassColor(texIndex) || useFoliageColor(texIndex))
    {
        float3 faceBiasPos = -normal * 1e-4; 
        // normal vector의 반대방향으로 shrink
        // bias를 사용하지 않으면 depthFighting같은 효과가 나타남
        // 1.0000001, 0.99999999가 서로 완전 다른 결과이기 때문
        float2 diffOffsetPos = floor(worldPos.xz + faceBiasPos.xz) - floor(eyePos.xz);
                
        float texelSize = 1.0 / (CHUNK_COUNT * CHUNK_SIZE);
        float2 climateTexcoord = float2(0.5 + diffOffsetPos.x * texelSize, 0.5 - diffOffsetPos.y * texelSize);
        climateTexcoord += float2(texelSize * 0.5, texelSize * 0.5);
        // texelSize * 0.5 만큼 더해주는 이유
        // 텍스쳐가 4x4의 형태에서 텍스쳐 좌표가 0.5, 0.5라면 (2, 2)의 중간에서 샘플링해야 함
        // 그렇지 않으면 diffOffsetPos의 연산 오차로 인해서 조금만 변해도 다른 텍셀을 샘플링하게 됨
        
        float2 th = climateNoiseMap.SampleLevel(pointClampSS, climateTexcoord, 0.0).rg;
        
        float3 climateColor = float3(0.0, 0.0, 0.0);
        if (useGrassColor(texIndex))
            climateColor = grassColorMap.SampleLevel(pointClampSS, float2(th.x, 1.0 - th.y), 0.0).rgb;
        if (useFoliageColor(texIndex))
            climateColor = foliageColorMap.SampleLevel(pointClampSS, float2(th.x, 1.0 - th.y), 0.0).rgb;
            
        albedo.rgb *= climateColor;
    }
    
    if (useDirtOverlay(texIndex))
    {
        float4 dirt = blockAtlasTextureArray.Sample(pointWrapSS, float3(texcoord, 3));
        albedo = lerp(dirt, albedo, albedo.a);
    }
    
    return albedo;
}

float3 getTangent(float3 normal)
{
    if (normal.x > 0 && normal.y == 0 && normal.z == 0)
    {
        return float3(0.0, 0.0, 1.0);
    }
    else if (normal.x < 0 && normal.y == 0 && normal.z == 0)
    {
        return float3(0.0, 0.0, -1.0);
    }
    else if (normal.y < 0 && normal.x == 0 && normal.z == 0)
    {
        return float3(1.0, 0.0, 0.0);
    }
    else if (normal.y > 0 && normal.x == 0 && normal.z == 0)
    {
        return float3(1.0, 0.0, 0.0);
    }
    else if (normal.z < 0 && normal.x == 0 && normal.y == 0)
    {
        return float3(1.0, 0.0, 0.0);

    }
    else if (normal.z > 0 && normal.x == 0 && normal.y == 0)
    {
        return float3(-1.0, 0.0, 0.0);
    }
    else
    {
        return float3(0.0, 0.0, 0.0);
    }
}

float3 normalMapping(float2 texcoord, uint texIndex, float3 normal)
{
    float3 normalTex = normalAtlasTextureArray.Sample(pointWrapSS, float3(texcoord, texIndex)).rgb;
    normalTex = normalize(2.0 * normalTex - 1.0); // TBN 스페이스에 존재하는 TBN 좌표
    
    // 월드 좌표를 기준으로 TBN축을 정의
    float3 N = normal; // N` 
    float3 T = getTangent(normal); // T`
    float3 B = cross(N, T); // B`
        
    float3x3 TBN = float3x3(T, B, N); // T`B`N`
    
    // Review
    // 월드 좌표를 기준으로 정의한 TBN축에 TBN스페이스 좌표를 곱하면 월드 좌표
    // 기하적으로 직접 해보면 됨
    return normalize(mul(normalTex, TBN)); // TS * TBN * M --> W
}

psOutput
    main(psInput
    input, 
    uint coverage : SV_COVERAGE, uint sampleIndex : SV_SampleIndex)
{
#ifdef USE_ALPHA_CLIP 
    if (blockAtlasTextureArray.SampleLevel(pointWrapSS, float3(input.texcoord, input.texIndex), 0.0).a != 1.0)
        discard;
    
    [unroll]
    for (uint i = 0; i < SAMPLE_COUNT; ++i)
    {
        float2 offsets[SAMPLE_COUNT] = { float2(0, -1), float2(0, 1), float2(-1, 0), float2(1, 0) };
        // 주변이 alpha clip이라면 coverage를 직접 본인 샘플 인덱스로 설정
        // 정확한 coverage값은 아님
        // SSAO에서는 coverage에 따라 weight를 두고 연산하지만 weight가 모두 1인 상태라고 보면 됨
        // Lighting에서는 coverage 구분 없이 그냥 4번 연산함 -> albedo가 다르기 때문
        if (blockAtlasTextureArray.SampleLevel(pointWrapSS, float3(input.texcoord, input.texIndex), 0.0, offsets[i]).a != 1.0)
        {
            coverage = (1 << sampleIndex);
        }
    }
#endif
    
    psOutput output;
    
    bool edge = (coverage != 0xf); // 0b1111 -> 1111은 모서리가 아닌 픽셀임
    
    float3 normal = normalMapping(input.texcoord, input.texIndex, input.normal);
    
    output.normalEdge = float4(normalize(normal), float(edge));
    
    output.position = float4(input.posWorld, 1.0);
    
    output.coverage = coverage;
    
    output.albedo = getAlbedo(input.texcoord, input.texIndex, input.posWorld, input.normal);
    
    output.mer = merAtlasTextureArray.Sample(pointWrapSS, float3(input.texcoord, input.texIndex));
    
    return output;
}

float4 mainMirror(psInput input) : SV_TARGET
{
#ifdef USE_ALPHA_CLIP 
    if (blockAtlasTextureArray.SampleLevel(pointWrapSS, float3(input.texcoord, input.texIndex), 0.0).a != 1.0)
        discard;
#endif
    
    float2 screenTexcoord = float2(input.posProj.x / mirrorWidth, input.posProj.y / mirrorHeight);
    float planeDepth = mirrorDepthTex.Sample(linearClampSS, screenTexcoord).r;
    float pixelDepth = input.posProj.z;

    if (pixelDepth <= planeDepth) // 거울보다 가까운 미러월드는 필요 없음
        discard;
    
    float4 albedo = getAlbedo(input.texcoord, input.texIndex, input.posWorld, input.normal);
    
    float3 mer = merAtlasTextureArray.Sample(pointWrapSS, float3(input.texcoord, input.texIndex)).rgb;
    
    float3 ambient = getAmbientLighting(1.0, albedo.rgb, input.posWorld, input.normal, mer.r, mer.b);
    
    return float4(ambient, albedo.a);
}