#ifndef KUJATA_OBJECT3D_PIXEL_HLSLI
#define KUJATA_OBJECT3D_PIXEL_HLSLI

// Object3d のピクセルシェーダーが使う、テクスチャ・サンプラー・定数バッファと共通の関数。
// エンジン標準(Object3d.PS.hlsl)と、マテリアルで選ぶ自作シェーダー(Object3dCustom.hlsli 経由)の両方が読む。
// レジスタの割り当ては 3d/GraphicsPipeline.cpp の CreateObject3dRootSignature と一致させること。
#include "Object3d.hlsli"

Texture2D<float32_t4> gTexture : register(t0);
// エミッションマップ(自己発光の分布)。未指定のマテリアルには白1x1が入るので常に乗算してよい。
Texture2D<float32_t4> gEmissiveTexture : register(t2);
SamplerState gSampler : register(s0);
// ぼかさずに読むサンプラー(ポイントサンプリング)。マテリアルの pointSampling で選ぶ。
SamplerState gPointSampler : register(s1);

struct Material
{
    float32_t4 color;
    int32_t enableLighting;
    float32_t4x4 uvTransform;
    float32_t shininess;
    float32_t3 emissiveColor;    // 自己発光色(リニア)
    float32_t emissiveIntensity; // 発光強度(>1でHDR輝度になりブルームが乗る)
    int32_t emissiveEnabled;     // マテリアルのEmissionチェック(0=発光しない)
    float32_t bloomIntensity;    // 露出光(滲み)の強さ(エミッションRTへ書く値のスケール)
    float32_t bloomThreshold;    // この輝度以上のエミッションだけが滲む(0=全て)
    float32_t bloomSoftKnee;     // 閾値の柔らかさ(0=ハード)
    float32_t triplanarScale;    // >0でワールド座標貼り。1ワールドユニットあたりの繰り返し数(0でUV貼り)
    // トゥーン(enableLighting == 8)。C++側 3d/GraphicsPipeline.h の MaterialData と並びを一致させること。
    // 影の色はマテリアルではなく世界共通(gDirectionalLight.shadowColor)。
    int32_t toonSteps;           // 明るさを何段に分けるか(2以上)
    float32_t toonSmoothness;    // 段の境目のぼかし幅(明るさの単位。0=くっきり)
    int32_t flatShading;         // 1=面ごとに平らな陰(どの方式でも効く)
    int32_t pointSampling;       // 1=テクスチャをぼかさずに読む(どの方式でも効く)
};

ConstantBuffer<Material> gMaterial : register(b0);
struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
    // エミッション専用RT(MRT)。ここに書いた値だけがブルーム(露出光)の入力になるため、
    // Emissionチェックの無いマテリアルや、単に明るいだけのピクセルは滲まない。
    float32_t4 emission : SV_TARGET1;
};
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);
ConstantBuffer<Camera> gCamera : register(b2);


static const uint32_t kMaxPointLight = 16;
struct PointLight
{
    float32_t4 color;   // !< ライトの色
    float32_t3 position;// !< ライトの位置
    float32_t intensity;// !< 輝度
    float32_t radius;   // !< ライトの届く最大距離
    float32_t decay;    // !< 減衰率
    float32_t2 padding;
};
cbuffer gPointLight : register(b3)
{
    PointLight pointLights[kMaxPointLight];
    int32_t pointLightCount;
};

struct SpotLight
{
    float32_t4 color;   // !< ライトの色
    float32_t3 position;// !< ライトの位置
    float32_t intensity;// !< 輝度
    float32_t3 direction; // !< スポットライトの方向
    float32_t distance; // !< ライトの届く最大距離
    float32_t decay; // !< 減衰率
    float32_t cosAngle; // スポットライトの余弦
    float32_t cosFalloffStart;
    // cbuffer配列の1要素を64バイト(16×4)に揃えるためのpadding。
    // C++側 3d/SpotLight.h の SpotLightData と必ず一致させること(static_assertで64固定)。
    float32_t padding;
};

static const uint32_t kMaxSpotLight = 16;
cbuffer gSpotLight : register(b4)
{
    SpotLight spotLights[kMaxSpotLight];
    int32_t spotLightCount;
};

// マテリアルの pointSampling に応じて、ぼかす/ぼかさないサンプラーで読む。
float32_t4 SampleTexture(Texture2D<float32_t4> tex, float32_t2 uv)
{
    return (gMaterial.pointSampling != 0) ? tex.Sample(gPointSampler, uv) : tex.Sample(gSampler, uv);
}

// ワールド座標を3軸から投影してサンプリングする(トライプラナー)。
//
// **プリミティブのUVは面ごとに0..1固定**なので、Transformで引き伸ばした箱に模様を貼ると、
// 面の実寸に関係なく必ず「1面あたりn枚」になる。scale(3,10,54)の壁なら、54ユニットの面も
// 10ユニットの面も同じ枚数が乗り、5倍以上に伸びた縞になってしまう。
// ワールド座標で貼れば、どの面でもどのオブジェクトでも密度が揃う(継ぎ目も出ない)。
//
// 法線の絶対値を重みに3方向をブレンドする。斜め面では2〜3枚が混ざるが、
// タイル可能なノイズなら混ざっても破綻しない。
float32_t4 SampleTriplanar(float32_t3 worldPosition, float32_t3 normal, float32_t scale)
{
    float32_t3 weight = abs(normalize(normal));
    weight /= max(weight.x + weight.y + weight.z, 1e-4f);

    float32_t4 sampleX = SampleTexture(gTexture, worldPosition.zy * scale);
    float32_t4 sampleY = SampleTexture(gTexture, worldPosition.xz * scale);
    float32_t4 sampleZ = SampleTexture(gTexture, worldPosition.xy * scale);

    return sampleX * weight.x + sampleY * weight.y + sampleZ * weight.z;
}

// トゥーン: 0..1 の明るさを toonSteps 段に丸める。戻り値も 0..1(0=いちばん暗い段、1=いちばん明るい段)。
// 例: 4段なら 0 / 0.33 / 0.67 / 1 の4通り。toonSmoothness が 0 より大きければ、段の境目だけを滑らかにつなぐ。
float32_t ToonStep(float32_t lightAmount)
{
    float32_t steps = (float32_t)max(gMaterial.toonSteps, 2);
    float32_t scaled = saturate(lightAmount) * steps;
    float32_t band = floor(scaled);
    float32_t width = saturate(gMaterial.toonSmoothness * steps);
    if (width > 0.0f)
    {
        band += smoothstep(1.0f - width, 1.0f, frac(scaled));
    }
    return min(band, steps - 1.0f) / (steps - 1.0f);
}

// 面ごとの法線(フラットシェーディング)。隣のピクセルとのワールド座標の差から面の向きを求めるので、
// 頂点の法線がなめらかに補間されていても、三角形ごとに平らな陰になる。
// 外積の向きは画面の向きで変わるので、カメラ側を向くようにそろえる(見えている面は必ずカメラ側を向く)。
float32_t3 FlatNormal(float32_t3 worldPosition)
{
    float32_t3 normal = normalize(cross(ddx(worldPosition), ddy(worldPosition)));
    float32_t3 toEye = gCamera.worldPosition - worldPosition;
    return (dot(normal, toEye) < 0.0f) ? -normal : normal;
}

#endif // KUJATA_OBJECT3D_PIXEL_HLSLI
