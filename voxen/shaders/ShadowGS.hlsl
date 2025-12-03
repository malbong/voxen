cbuffer ShadowConstantBuffer : register(b0)
{
    Matrix viewProj[3];
}

struct gsInput
{
    float4 posWorld : SV_POSITION;
#ifdef USE_INSTANCE
    float2 texcoord : TEXCOORD;
    uint texIndex : INDEX;
#endif
};

struct gsOutput
{
    float4 pos : SV_POSITION;
#ifdef USE_INSTANCE
    float2 texcoord : TEXCOORD;
    uint texIndex : INDEX;
#endif
    uint VPIndex : SV_ViewportArrayIndex;
};

[maxvertexcount(9)]
void main(triangle gsInput input[3], inout TriangleStream<gsOutput> output)
{
    gsOutput element;
    
    for (int cascade = 0; cascade < 3; ++cascade)
    {
        element.VPIndex = cascade;

        for (int i = 0; i < 3; ++i)
        {
            float4 position = float4(input[i].posWorld.xyz, 1.0);
            element.pos = mul(position, viewProj[cascade]);
            
#ifdef USE_INSTANCE
            element.texcoord = input[i].texcoord;
            element.texIndex = input[i].texIndex;
#endif
            output.Append(element);
        }
        output.RestartStrip();
    }
}