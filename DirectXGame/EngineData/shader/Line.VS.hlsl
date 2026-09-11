struct LineVertexInput
{
    float32_t3 position : POSITION0;
    float32_t4 color : COLOR0;
};

struct LineVertexOutput
{
    float32_t4 position : SV_POSITION;
    float32_t4 color : COLOR0;
};

struct LineTransform
{
    float32_t4x4 WVP;
};

ConstantBuffer<LineTransform> gLineTransform : register(b0);

LineVertexOutput main(LineVertexInput input)
{
    LineVertexOutput output;
    output.position = mul(float32_t4(input.position, 1.0f), gLineTransform.WVP);
    output.color = input.color;
    return output;
}
