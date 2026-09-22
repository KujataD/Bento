// エンジン同梱の海(OceanComponent が使う。マテリアルの Shader に engine:Custom/Ocean.hlsl を選べば、ほかの物にも使える)。
// ローポリ・ドット絵に合わせて、陰も色の変化もすべて「段」で切り替える:
//   トゥーンの陰(面ごとに平ら) / 波の谷を少し濃く / 波の山に泡(白)
//
// 波は正弦波を最大4つ足したもの(上下にだけ動かす)。**OceanComponent::EvaluateWaves(C++)と同じ式**にしておくこと。
// ずれると、浮かぶ物(FloatOnWaterComponent)や水弾が海面の高さと合わなくなる。
//
// gShaderParams.params[i] = (向き[度], 波長[m], 高さ[m], 速さ[m/s])  i = 0〜3。波長が0の波は使わない
// gShaderParams.userValue = 泡が出る高さ(0〜1。0=いちばん低い谷、1=いちばん高い山。0 なら泡なし)
// gShaderParams.userValue2 = 海の基準の高さ(オブジェクトのワールドの Y)
// OceanComponent がこれらを毎フレーム入れる(マテリアルの Shader Params は使わない)。
#include "Object3dCustom.hlsli"

// 位置 xz[m] の波の高さ。maxHeight には波の高さの合計(山の最大)を返す。
float32_t WaveHeight(float32_t2 xz, out float32_t maxHeight)
{
    float32_t height = 0.0f;
    maxHeight = 0.0f;
    [unroll]
    for (int32_t i = 0; i < 4; ++i)
    {
        const float32_t4 wave = gShaderParams.params[i];
        if (wave.y <= 0.0f)
        {
            continue;
        }
        const float32_t angle = radians(wave.x);
        const float32_t2 direction = float32_t2(cos(angle), sin(angle));
        const float32_t k = 6.2831853f / wave.y;
        height += wave.z * sin(k * (dot(direction, xz) - wave.w * gShaderParams.time));
        maxHeight += abs(wave.z);
    }
    return height;
}

#ifdef KUJATA_VERTEX_SHADER
VertexShaderOutput VSMain(VertexShaderInput input)
{
    // 波はワールド座標の xz で決める(海を動かしても、波の模様はその場に留まる)。
    const float32_t3 world = mul(input.position, gTranformationMatrix.World).xyz;
    float32_t maxHeight;
    input.position.y += WaveHeight(world.xz, maxHeight);
    return DefaultVertex(input);
}
#endif

#ifdef KUJATA_PIXEL_SHADER
PixelShaderOutput PSMain(VertexShaderOutput input)
{
    // 陰(トゥーン。段の数はマテリアルの Toon Steps、影の色は世界共通)。Flat Shading なら面ごとに平らに。
    const float32_t3 normal = (gMaterial.flatShading != 0) ? FlatNormal(input.worldPosition) : normalize(input.normal);
    const float32_t2 uv = mul(float32_t4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform).xy;
    const float32_t4 textureColor = SampleTexture(gTexture, uv);
    const float32_t3 albedo = gMaterial.color.rgb * textureColor.rgb;
    const float32_t lit = ToonStep(saturate(dot(normal, -normalize(gDirectionalLight.direction))));
    const float32_t3 shadow = albedo * gDirectionalLight.shadowColor;
    const float32_t3 bright = max(albedo * gDirectionalLight.color.rgb * gDirectionalLight.intensity, shadow);
    float32_t3 color = lerp(shadow, bright, lit);

    // 波のどのあたりか(0=いちばん低い谷 〜 1=いちばん高い山)。波の式をピクセルごとに計算せず、
    // 頂点の高さを補間した値(= 描かれている面の高さ)を使う。三角形の中で直線的に変わるので、泡の境目が面に沿ってカクカクになる。
    float32_t maxHeight;
    WaveHeight(input.worldPosition.xz, maxHeight);
    const float32_t height = input.worldPosition.y - gShaderParams.userValue2;
    const float32_t crest = maxHeight > 0.0f ? saturate(height / maxHeight * 0.5f + 0.5f) : 0.5f;

    // 谷は1段だけ濃く、山の上の方は泡で白くする(どちらも段で切り替える)。
    if (crest < 0.3f)
    {
        color *= 0.82f;
    }
    const float32_t foamLevel = gShaderParams.userValue;
    if (foamLevel > 0.0f && crest > foamLevel)
    {
        color = lerp(color, float32_t3(1.0f, 1.0f, 1.0f), 0.7f);
    }

    PixelShaderOutput output;
    output.color = float32_t4(color, gMaterial.color.a * textureColor.a);
    output.emission = float32_t4(0.0f, 0.0f, 0.0f, 1.0f);
    return output;
}
#endif
