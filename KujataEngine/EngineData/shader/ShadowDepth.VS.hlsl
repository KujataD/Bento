struct TransformationMatrix
{
    float32_t4x4 WVP;
    float32_t4x4 World;
    float32_t4x4 WorldInverseTranspose;
};
ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);

struct VertexShaderInput
{
    float32_t4 position : POSITION0;
};

float32_t4 main(VertexShaderInput input) : SV_POSITION
{
    // Step1で確保したシャドウ用の枠に、ライト視点のWVPが入っている。
    return mul(input.position, gTransformationMatrix.WVP);
}