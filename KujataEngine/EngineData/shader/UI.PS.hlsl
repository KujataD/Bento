// UI用の最小ピクセルシェーダ。
// 通常(Image): テクスチャ * 色。
// フォント(Text): SDF(符号付き距離場)テクスチャからスケール非依存のアンチエイリアスを生成する。
struct Material
{
    float4 color;
    float4x4 uvTransform;
    float4 flags; // x: SDFフォント使用(1=フォント)
};
ConstantBuffer<Material> gMaterial : register(b0);
Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PixelShaderInput input) : SV_TARGET
{
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);

    if (gMaterial.flags.x > 0.5f)
    {
        // SDFフォント: 距離場(エッジ=0.5)から、画面空間の勾配に応じたAA幅でカバレッジを作る。
        // fwidthは表示スケールに追従するため、どの拡大率でもエッジがくっきりする。
        float dist = textureColor.r;
        float aa = fwidth(dist);
        aa = max(aa, 1.0f / 1024.0f); // 勾配ゼロ(縮退)でのAA消失を防ぐ最小幅。
        float coverage = smoothstep(0.5f - aa, 0.5f + aa, dist);
        clip(coverage - 0.003f); // 完全に外側のピクセルは破棄。
        // 色はmaterialから、カバレッジはSDFから。
        return float4(gMaterial.color.rgb, coverage * gMaterial.color.a);
    }

    return textureColor * gMaterial.color;
}
