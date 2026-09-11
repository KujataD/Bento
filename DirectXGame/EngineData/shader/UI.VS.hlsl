// UI(スクリーン空間)用の最小頂点シェーダ。
// 頂点はピクセル座標で与えられ、gTransform.WVP(オルソ投影)でクリップ空間へ変換する。
struct TransformationMatrix
{
    float4x4 WVP;
};
ConstantBuffer<TransformationMatrix> gTransform : register(b0);

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0; // 入力レイアウト共通化のため受けるが未使用。
};

struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    output.position = mul(input.position, gTransform.WVP);
    output.texcoord = input.texcoord;
    return output;
}
