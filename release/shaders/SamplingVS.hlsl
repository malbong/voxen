struct vsInput
{
    float3 position : POSITION;
    float2 texcoord : TEXCOORD;
};

struct vsOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

vsOutput main(vsInput input)
{
    vsOutput output;
    
    output.position = float4(input.position, 1.0);
    output.texcoord = input.texcoord;

    return output;
}
