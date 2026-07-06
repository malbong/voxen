#include "Common.hlsli"

cbuffer ChunkConstantBuffer : register(b0)
{
    matrix world;
}

struct vsInput
{
    uint data : DATA;
};

struct vsOutput
{
#ifdef USE_SHADOW
    float4 posProj : SV_POSITION;
#else
    float4 posProj : SV_POSITION;
    sample float3 posWorld : POSITION;
    sample float3 normal : NORMAL;
    sample float2 texcoord : TEXCOORD;
    uint texIndex : INDEX;
#endif
};

float2 getVoxelTexcoord(float3 pos, uint face)
{
    float2 texcoord = float2(0.0, 0.0);
    
    if (face == LEFT)
    {
        texcoord = float2(-pos.z + CHUNK_SIZE, -pos.y + CHUNK_SIZE);
    }
    else if (face == RIGHT)
    {
        texcoord = float2(pos.z, -pos.y + CHUNK_SIZE);
    }
    else if (face == BOTTOM)
    {
        texcoord = float2(pos.x, pos.z);
    }
    else if (face == TOP)
    {
        texcoord = float2(pos.x, -pos.z + CHUNK_SIZE);
    }
    else if (face == NEAR)
    {
        texcoord = float2(pos.x, -pos.y + CHUNK_SIZE);
    }
    else // FAR
    {
        texcoord = float2(-pos.x + CHUNK_SIZE, -pos.y + CHUNK_SIZE);
    }

    return texcoord;
}

float3 getNormal(uint face)
{
    static const float3 normals[6] =
    {
        float3(-1, 0, 0), // LEFT (0)
        float3(1, 0, 0),  // RIGHT (1)
        float3(0, -1, 0), // BOTTOM (2)
        float3(0, 1, 0),  // TOP (3)
        float3(0, 0, -1), // NEAR (4)
        float3(0, 0, 1)   // FAR (5)
    };

    return normals[face];
}

vsOutput main(vsInput input)
{
    vsOutput output;
    
    int x = (input.data >> 23) & 63;
    int y = (input.data >> 17) & 63;
    int z = (input.data >> 11) & 63;
    uint face = (input.data >> 8) & 7;
    uint texIndex = input.data & 255;
    
    float3 position = float3(float(x), float(y), float(z));
    
    output.posProj = mul(float4(position, 1.0), world);
    
#ifdef USE_SHADOW
    return output;
#else
    
    output.posWorld = output.posProj.xyz;
    
    output.posProj = mul(output.posProj, view);
    output.posProj = mul(output.posProj, proj);
    
    output.normal = getNormal(face);
    #ifdef USE_ALPHA_CLIP
        float3 toEye = normalize(eyePos - output.posWorld);
        if (dot(output.normal, toEye) < 0.0)
            output.normal *= -1;
    #endif
    
    output.texcoord = getVoxelTexcoord(position, face);
    output.texIndex = texIndex;

    return output;
#endif
}