struct LinePixelInput
{
    float32_t4 position : SV_POSITION;
    float32_t4 color : COLOR0;
};

struct LinePixelOutput
{
    float32_t4 color : SV_TARGET0;
};

LinePixelOutput main(LinePixelInput input)
{
    LinePixelOutput output;
    output.color = input.color;
    return output;
}
