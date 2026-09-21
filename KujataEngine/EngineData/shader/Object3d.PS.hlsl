#include "object3d.hlsli"

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

// --- バリア(enableLighting == 6)用の定数 ---------------------------------
// 切頂二十面体(サッカーボール)のセル中心方向。
// 中心は「正二十面体の頂点12個(=五角形の中心)」+「正十二面体の頂点20個(=六角形の中心)」の計32個で、
// この集合は原点対称なので、対になる向きは abs(dot) でまとめて16本ぶんだけ持てばよい。
// 法線方向に対して最も近い中心と2番目に近い中心の差からセルの境界を出す(球面ボロノイ)。
static const float32_t3 kBarrierCells[16] =
{
    // 正二十面体の頂点(五角形セルの中心) 6本
    float32_t3(0.0000000f,  0.5257311f,  0.8506508f),
    float32_t3(0.0000000f, -0.5257311f,  0.8506508f),
    float32_t3(0.5257311f,  0.8506508f,  0.0000000f),
    float32_t3(-0.5257311f, 0.8506508f,  0.0000000f),
    float32_t3(0.8506508f,  0.0000000f,  0.5257311f),
    float32_t3(0.8506508f,  0.0000000f, -0.5257311f),
    // 正十二面体の頂点(六角形セルの中心) 10本
    float32_t3(0.5773503f,  0.5773503f,  0.5773503f),
    float32_t3(0.5773503f,  0.5773503f, -0.5773503f),
    float32_t3(0.5773503f, -0.5773503f,  0.5773503f),
    float32_t3(0.5773503f, -0.5773503f, -0.5773503f),
    float32_t3(0.0000000f,  0.3568221f,  0.9341724f),
    float32_t3(0.0000000f, -0.3568221f,  0.9341724f),
    float32_t3(0.3568221f,  0.9341724f,  0.0000000f),
    float32_t3(-0.3568221f, 0.9341724f,  0.0000000f),
    float32_t3(0.9341724f,  0.0000000f,  0.3568221f),
    float32_t3(0.9341724f,  0.0000000f, -0.3568221f),
};

// セルの境界線の太さ(最近傍と次近傍の内積差のしきい値)。大きいほど枠が太い。
static const float32_t kBarrierLineWidth = 0.045f;
// セル内側の不透明度(Base Colorのαに対する割合)。
static const float32_t kBarrierFillAlpha = 0.18f;
// 縁(視線と垂直な部分)の光り方。ドーム状に見せるためのフレネル。
static const float32_t kBarrierRimPower = 2.5f;

// 衝撃波の輪の断面。外側(進行方向)を鋭く、内側を長く尾引かせると「通り過ぎた圧」に見える。
static const float32_t kShockwaveFrontWidth = 0.35f;
static const float32_t kShockwaveTailWidth = 0.75f;

// トレイルの幅方向のぼかし量。大きいほど縁が柔らかい。
static const float32_t kTrailEdgeSoftness = 0.6f;

// ワールド座標を3軸から投影してサンプリングする(トライプラナー)。
//
// **プリミティブのUVは面ごとに0..1固定**なので、Transformで引き伸ばした箱に模様を貼ると、
// 面の実寸に関係なく必ず「1面あたりn枚」になる。scale(3,10,54)の壁なら、54ユニットの面も
// 10ユニットの面も同じ枚数が乗り、5倍以上に伸びた縞になってしまう。
// ワールド座標で貼れば、どの面でもどのオブジェクトでも密度が揃う(継ぎ目も出ない)。
//
// 法線の絶対値を重みに3方向をブレンドする。斜め面では2〜3枚が混ざるが、
// タイル可能なノイズなら混ざっても破綻しない。
// マテリアルの pointSampling に応じて、ぼかす/ぼかさないサンプラーで読む。
float32_t4 SampleTexture(Texture2D<float32_t4> tex, float32_t2 uv)
{
    return (gMaterial.pointSampling != 0) ? tex.Sample(gPointSampler, uv) : tex.Sample(gSampler, uv);
}

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

PixelShaderOutput main(VertexShaderOutput input)
{
    // フラットシェーディング: 以降のすべての方式が、頂点の法線の代わりに面の法線を使う。
    if (gMaterial.flatShading != 0)
    {
        input.normal = FlatNormal(input.worldPosition);
    }
    float32_t4 transformedUV = mul(float32_t4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    // triplanarScaleが0のときは今までどおりメッシュのUVで貼る(スクロールや板ポリはこちら)。
    float32_t4 textureColor = (gMaterial.triplanarScale > 0.0f)
        ? SampleTriplanar(input.worldPosition, input.normal, gMaterial.triplanarScale)
        : SampleTexture(gTexture, transformedUV.xy);
    PixelShaderOutput output;
    // エミッションRTは既定で書き込みなし(黒)。Emissionチェック付きマテリアルだけが下で上書きする。
    output.emission = float32_t4(0.0f, 0.0f, 0.0f, 1.0f);
    // ライティング各処理
    if (gMaterial.enableLighting != 0)
    { // Lightingする場合
    
        if (gMaterial.enableLighting == 1)
        { // lambertModel (DirectionalLight + PointLight + SpotLight の拡散反射)
            float32_t3 normal = normalize(input.normal);

            // DirectionalLight
            float cos = saturate(dot(normal, -normalize(gDirectionalLight.direction)));
            float32_t3 result = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * cos * gDirectionalLight.intensity;

            // PointLights (拡散反射)
            for (int32_t i = 0; i < pointLightCount; i++)
            {
                float32_t3 pointLightDirection = normalize(pointLights[i].position - input.worldPosition);
                float32_t pointLightCos = saturate(dot(normal, pointLightDirection));
                float32_t distance = length(pointLights[i].position - input.worldPosition);
                float32_t factor = pow(saturate(-distance / pointLights[i].radius + 1.0f), pointLights[i].decay);
                result += gMaterial.color.rgb * textureColor.rgb * pointLights[i].color.rgb * pointLightCos * pointLights[i].intensity * factor;
            }

            // SpotLights (拡散反射)
            for (int32_t s = 0; s < spotLightCount; s++)
            {
                float32_t3 spotLightDirection = normalize(spotLights[s].position - input.worldPosition);
                float32_t spotLightCos = saturate(dot(normal, spotLightDirection));
                float32_t3 spotLightDirectionOnSurface = normalize(input.worldPosition - spotLights[s].position);
                float32_t cosAngle = dot(spotLightDirectionOnSurface, normalize(spotLights[s].direction));
                float32_t falloffFactor = saturate((cosAngle - spotLights[s].cosAngle) / (spotLights[s].cosFalloffStart - spotLights[s].cosAngle));
                float32_t distance = length(spotLights[s].position - input.worldPosition);
                float32_t attenuationFactor = pow(saturate(-distance / spotLights[s].distance + 1.0f), spotLights[s].decay);
                result += gMaterial.color.rgb * textureColor.rgb * spotLights[s].color.rgb * spotLightCos * spotLights[s].intensity * attenuationFactor * falloffFactor;
            }

            output.color.rgb = result;
            output.color.a = gMaterial.color.a * textureColor.a;
        }
        else if (gMaterial.enableLighting == 2)
        { // halfLambertModel (DirectionalLight + PointLight + SpotLight の拡散反射)
            float32_t3 normal = normalize(input.normal);

            // DirectionalLight
            float NdotL = dot(normal, -normalize(gDirectionalLight.direction));
            float cos = pow(NdotL * 0.5f + 0.5f, 2.0f);
            float32_t3 result = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * cos * gDirectionalLight.intensity;

            // PointLights (ハーフランバート拡散)
            for (int32_t i = 0; i < pointLightCount; i++)
            {
                float32_t3 pointLightDirection = normalize(pointLights[i].position - input.worldPosition);
                float32_t pointNdotL = dot(normal, pointLightDirection);
                float32_t pointLightCos = pow(pointNdotL * 0.5f + 0.5f, 2.0f);
                float32_t distance = length(pointLights[i].position - input.worldPosition);
                float32_t factor = pow(saturate(-distance / pointLights[i].radius + 1.0f), pointLights[i].decay);
                result += gMaterial.color.rgb * textureColor.rgb * pointLights[i].color.rgb * pointLightCos * pointLights[i].intensity * factor;
            }

            // SpotLights (ハーフランバート拡散)
            for (int32_t s = 0; s < spotLightCount; s++)
            {
                float32_t3 spotLightDirection = normalize(spotLights[s].position - input.worldPosition);
                float32_t spotNdotL = dot(normal, spotLightDirection);
                float32_t spotLightCos = pow(spotNdotL * 0.5f + 0.5f, 2.0f);
                float32_t3 spotLightDirectionOnSurface = normalize(input.worldPosition - spotLights[s].position);
                float32_t cosAngle = dot(spotLightDirectionOnSurface, normalize(spotLights[s].direction));
                float32_t falloffFactor = saturate((cosAngle - spotLights[s].cosAngle) / (spotLights[s].cosFalloffStart - spotLights[s].cosAngle));
                float32_t distance = length(spotLights[s].position - input.worldPosition);
                float32_t attenuationFactor = pow(saturate(-distance / spotLights[s].distance + 1.0f), spotLights[s].decay);
                result += gMaterial.color.rgb * textureColor.rgb * spotLights[s].color.rgb * spotLightCos * spotLights[s].intensity * attenuationFactor * falloffFactor;
            }

            output.color.rgb = result;
            output.color.a = gMaterial.color.a * textureColor.a;
        }
        else if (gMaterial.enableLighting == 3)
        { // PhongReflectionModel (ランバート拡散 + Phong鏡面 + PointLight + SpotLight)
            float32_t3 normal = normalize(input.normal);
            // カメラへの方向を算出する。数式のv
            float32_t3 toEye = normalize(gCamera.worldPosition - input.worldPosition);

            // --- DirectionalLight ---
            // 拡散はランバート(saturate)にする。ハーフランバートだと裏面も明るく影が出ないため。
            float cos = saturate(dot(normal, -normalize(gDirectionalLight.direction)));
            float32_t3 reflectLight = reflect(normalize(gDirectionalLight.direction), normal);
            float32_t specularPow = pow(saturate(dot(reflectLight, toEye)), gMaterial.shininess);
            float32_t3 diffuse = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * cos * gDirectionalLight.intensity;
            float32_t3 specular = gDirectionalLight.color.rgb * gDirectionalLight.intensity * specularPow * float32_t3(1.0f, 1.0f, 1.0f);
            float32_t3 result = diffuse + specular;

            // --- PointLights ---
            for (int32_t i = 0; i < pointLightCount; i++)
            {
                float32_t3 pointLightDirection = normalize(pointLights[i].position - input.worldPosition);
                float32_t pointLightCos = saturate(dot(normal, pointLightDirection));
                float32_t3 pointReflect = reflect(-pointLightDirection, normal);
                float32_t pointSpecularPow = pow(saturate(dot(pointReflect, toEye)), gMaterial.shininess);
                float32_t distance = length(pointLights[i].position - input.worldPosition);
                float32_t factor = pow(saturate(-distance / pointLights[i].radius + 1.0f), pointLights[i].decay);
                result += gMaterial.color.rgb * textureColor.rgb * pointLights[i].color.rgb * pointLightCos * pointLights[i].intensity * factor;
                result += pointLights[i].color.rgb * pointLights[i].intensity * pointSpecularPow * float32_t3(1.0f, 1.0f, 1.0f) * factor;
            }

            // --- SpotLights ---
            for (int32_t s = 0; s < spotLightCount; s++)
            {
                float32_t3 spotLightDirection = normalize(spotLights[s].position - input.worldPosition);
                float32_t spotLightCos = saturate(dot(normal, spotLightDirection));
                float32_t3 spotReflect = reflect(-spotLightDirection, normal);
                float32_t spotSpecularPow = pow(saturate(dot(spotReflect, toEye)), gMaterial.shininess);
                float32_t3 spotLightDirectionOnSurface = normalize(input.worldPosition - spotLights[s].position);
                float32_t cosAngle = dot(spotLightDirectionOnSurface, normalize(spotLights[s].direction));
                float32_t falloffFactor = saturate((cosAngle - spotLights[s].cosAngle) / (spotLights[s].cosFalloffStart - spotLights[s].cosAngle));
                float32_t distance = length(spotLights[s].position - input.worldPosition);
                float32_t attenuationFactor = pow(saturate(-distance / spotLights[s].distance + 1.0f), spotLights[s].decay);
                result += gMaterial.color.rgb * textureColor.rgb * spotLights[s].color.rgb * spotLightCos * spotLights[s].intensity * attenuationFactor * falloffFactor;
                result += spotLights[s].color.rgb * spotLights[s].intensity * spotSpecularPow * float32_t3(1.0f, 1.0f, 1.0f) * attenuationFactor * falloffFactor;
            }

            output.color.rgb = result;
            output.color.a = gMaterial.color.a * textureColor.a;
        }
        else if (gMaterial.enableLighting == 4)
        { // BlingPhongReflectionModel
            
            float NdotL = dot(normalize(input.normal), -normalize(gDirectionalLight.direction));
            float cos = pow(NdotL * 0.5f + 0.5f, 2.0f);
            // カメラへの方向を算出する。数式のv
            float32_t3 toEye = normalize(gCamera.worldPosition - input.worldPosition);
    
            // DirectionalLight計算処理
            // ----------------------------------
            
            // 入射角の反射ベクトルを求める。数式のr
            float32_t3 reflectLight = reflect(normalize(gDirectionalLight.direction), normalize(input.normal));

            // 内積をとって、saturateして、shininessを階乗すると鏡面反射の強度が求まる
            float32_t3 halfVector = normalize(-gDirectionalLight.direction + toEye);
            float NDotH = dot(normalize(input.normal), halfVector);
            float specularPow = pow(saturate(NDotH), gMaterial.shininess);
            
            // 拡散反射
            float32_t3 directionalLightDiffuse = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * cos * gDirectionalLight.intensity;
            // 鏡面反射
            float32_t3 directionalLightSpecular = gDirectionalLight.color.rgb * gDirectionalLight.intensity * specularPow * float32_t3(1.0f, 1.0f, 1.0f);
            // 拡散反射+鏡面反射
            output.color.rgb = directionalLightDiffuse + directionalLightSpecular;
            
            // PointLights実装
            // ----------------------------------
            for (int32_t i = 0; i < pointLightCount; i++)
            {
                // 方向
                float32_t3 pointLightDirection = normalize(pointLights[i].position - input.worldPosition);
                float32_t pointLightCos = saturate(dot(normalize(input.normal), pointLightDirection));
                float32_t3 halfVectorPoint = normalize(pointLightDirection + toEye);
                float32_t pointLightSpecularPow = pow(saturate(dot(normalize(input.normal), halfVectorPoint)), gMaterial.shininess);
                
 
                float32_t distance = length(pointLights[i].position - input.worldPosition); // ポイントライトへの距離
                float32_t factor = pow(saturate(-distance / pointLights[i].radius + 1.0f), pointLights[i].decay); // 指数によるコントロール
                
                // 拡散反射
                float32_t3 pointLightDiffuse = gMaterial.color.rgb * textureColor.rgb * pointLights[i].color.rgb * pointLightCos * pointLights[i].intensity * factor;
                // 鏡面反射
                float32_t3 pointLightSpecular = pointLights[i].color.rgb * pointLights[i].intensity * pointLightSpecularPow * float32_t3(1.0f, 1.0f, 1.0f) * factor;
                
                output.color.rgb += pointLightDiffuse + pointLightSpecular;
 
            }
            
            // SpotLights実装
            // ----------------------------------
            for (int32_t s = 0; s < spotLightCount; s++)
            {
                // 方向
                float32_t3 spotLightDirection = normalize(spotLights[s].position - input.worldPosition);
                float32_t spotLightCos = saturate(dot(normalize(input.normal), spotLightDirection));
                float32_t3 halfVectorSpot = normalize(spotLightDirection + toEye);
                float32_t spotLightSpecularPow = pow(saturate(dot(normalize(input.normal), halfVectorSpot)), gMaterial.shininess);

                float32_t3 spotLightDirectionOnSurface = normalize(input.worldPosition - spotLights[s].position);

                float32_t cosAngle = dot(spotLightDirectionOnSurface, normalize(spotLights[s].direction));
                float32_t falloffFactor = saturate((cosAngle - spotLights[s].cosAngle) / (spotLights[s].cosFalloffStart - spotLights[s].cosAngle));

                float32_t distance = length(spotLights[s].position - input.worldPosition);
                float32_t attenuationFactor = pow(saturate(-distance / spotLights[s].distance + 1.0f), spotLights[s].decay);

                // 拡散反射
                float32_t3 spotLightDiffuse = gMaterial.color.rgb * textureColor.rgb * spotLights[s].color.rgb * spotLightCos * spotLights[s].intensity * attenuationFactor * falloffFactor;
                // 鏡面反射
                float32_t3 spotLightSpecular = spotLights[s].color.rgb * spotLights[s].intensity * spotLightSpecularPow * float32_t3(1.0f, 1.0f, 1.0f) * attenuationFactor * falloffFactor;

                output.color.rgb += spotLightDiffuse + spotLightSpecular;

            }
            // アルファは今まで通り
            output.color.a = gMaterial.color.a * textureColor.a;
        }
        else if (gMaterial.enableLighting == 5)
        { // 衝撃波の輪(Ringプリミティブ前提)。土煙なので発光させず、帯の内外へ向けて薄くするだけ。
            // v は帯の内周(0)→外周(1)。両端を0にして、輪の境界が線で切れないようにする。
            float32_t band = input.texcoord.y;
            // 外側は鋭く立ち上がり、内側は長く尾を引く(圧が前面に集まり、後ろへ散っていく見え方)。
            float32_t front = smoothstep(0.0f, kShockwaveFrontWidth, 1.0f - band);
            float32_t tail = smoothstep(0.0f, kShockwaveTailWidth, band);
            float32_t profile = saturate(front * tail);

            output.color.rgb = gMaterial.color.rgb * textureColor.rgb;
            output.color.a = gMaterial.color.a * textureColor.a * profile;
            // 完全に透けた部分はPS末尾でdiscardされるので、半透明が深度バッファを汚さない。
        }
        else if (gMaterial.enableLighting == 7)
        { // トレイル(TrailRendererComponentの帯)。u=先頭(0)→末尾(1) / v=幅方向(0〜1)。
            // 末尾ほど薄くする。帯の終端が線でスパッと切れないようにするのが主目的。
            float32_t tailFade = 1.0f - input.texcoord.x;
            // 幅方向の縁もぼかして、板の輪郭が出ないようにする。
            float32_t edge = 1.0f - abs(input.texcoord.y * 2.0f - 1.0f);
            float32_t profile = saturate(tailFade * smoothstep(0.0f, kTrailEdgeSoftness, edge));

            output.color.rgb = gMaterial.color.rgb * textureColor.rgb;
            output.color.a = gMaterial.color.a * textureColor.a * profile;
        }
        else if (gMaterial.enableLighting == 8)
        { // トゥーン(セル調)。明るさを段に分け、暗い側は黒ではなく影の色(世界共通。Directional Light の Shadow Color)で塗る。
            float32_t3 normal = normalize(input.normal);
            float32_t3 albedo = gMaterial.color.rgb * textureColor.rgb;

            // DirectionalLight: いちばん暗い段 = 元の色×影の色、いちばん明るい段 = 元の色×ライトの色。その間を段で分ける。
            // ライトが影の色より暗い(強さ0など)ときに、光の当たる側が影より暗くならないよう、明るい段は影の色を下限にする。
            float32_t lit = ToonStep(saturate(dot(normal, -normalize(gDirectionalLight.direction))));
            float32_t3 shadow = albedo * gDirectionalLight.shadowColor;
            float32_t3 bright = max(albedo * gDirectionalLight.color.rgb * gDirectionalLight.intensity, shadow);
            float32_t3 result = lerp(shadow, bright, lit);

            // PointLights / SpotLights: 当たる量(角度×距離の減衰)を段に丸めて足す。
            for (int32_t i = 0; i < pointLightCount; i++)
            {
                float32_t3 pointLightDirection = normalize(pointLights[i].position - input.worldPosition);
                float32_t pointLightCos = saturate(dot(normal, pointLightDirection));
                float32_t distance = length(pointLights[i].position - input.worldPosition);
                float32_t factor = pow(saturate(-distance / pointLights[i].radius + 1.0f), pointLights[i].decay);
                result += albedo * pointLights[i].color.rgb * pointLights[i].intensity * ToonStep(pointLightCos * factor);
            }
            for (int32_t s = 0; s < spotLightCount; s++)
            {
                float32_t3 spotLightDirection = normalize(spotLights[s].position - input.worldPosition);
                float32_t spotLightCos = saturate(dot(normal, spotLightDirection));
                float32_t3 spotLightDirectionOnSurface = normalize(input.worldPosition - spotLights[s].position);
                float32_t cosAngle = dot(spotLightDirectionOnSurface, normalize(spotLights[s].direction));
                float32_t falloffFactor = saturate((cosAngle - spotLights[s].cosAngle) / (spotLights[s].cosFalloffStart - spotLights[s].cosAngle));
                float32_t distance = length(spotLights[s].position - input.worldPosition);
                float32_t attenuationFactor = pow(saturate(-distance / spotLights[s].distance + 1.0f), spotLights[s].decay);
                result += albedo * spotLights[s].color.rgb * spotLights[s].intensity * ToonStep(spotLightCos * attenuationFactor * falloffFactor);
            }

            output.color.rgb = result;
            output.color.a = gMaterial.color.a * textureColor.a;
        }
        else if (gMaterial.enableLighting == 6)
        { // バリア: 六角形20枚+五角形12枚のセルが浮かぶ半透明シェル
            // 球の法線はそのまま中心からの向きなので、UVを使わずにセルを求められる
            // (球のUVは極で潰れるため、UVで貼ると模様が歪む)。
            float32_t3 normal = normalize(input.normal);

            // 最も近いセル中心(d1)と2番目(d2)。対になる向きは abs でまとめている。
            float32_t d1 = -2.0f;
            float32_t d2 = -2.0f;
            [unroll]
            for (int32_t c = 0; c < 16; c++)
            {
                float32_t d = abs(dot(normal, kBarrierCells[c]));
                if (d > d1)
                {
                    d2 = d1;
                    d1 = d;
                }
                else if (d > d2)
                {
                    d2 = d;
                }
            }

            // d1とd2が拮抗している場所 = 2つのセルの境界。ここを線として光らせる。
            float32_t edge = 1.0f - smoothstep(0.0f, kBarrierLineWidth, d1 - d2);

            // 縁(視線に対して垂直に近い面)を光らせてドーム感を出す。
            float32_t3 toEye = normalize(gCamera.worldPosition - input.worldPosition);
            float32_t rim = pow(1.0f - saturate(abs(dot(normal, toEye))), kBarrierRimPower);

            float32_t glow = saturate(edge + rim * 0.6f);

            output.color.rgb = gMaterial.color.rgb * textureColor.rgb * (0.6f + glow * 2.0f);
            // 面の内側は薄く、枠と縁だけがはっきり出る。
            output.color.a = gMaterial.color.a * textureColor.a * saturate(kBarrierFillAlpha + glow);

            // 光る部分だけをブルームへ回す(一様に発光させると模様が潰れるため、
            // マテリアルのEmissionチェックには頼らずここで書く)。
            if (gMaterial.emissiveEnabled == 0)
            {
                float32_t3 barrierEmissive = gMaterial.color.rgb * glow * gMaterial.bloomIntensity;
                output.emission.rgb = min(barrierEmissive, 65000.0f);
            }
        }
        else
        {
            float cos = saturate(dot(normalize(input.normal), -gDirectionalLight.direction)); // lambertModel

            output.color.rgb = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * cos * gDirectionalLight.intensity;
            output.color.a = gMaterial.color.a * textureColor.a;
        }
    }
    else
    { //Lightingしない場合。前回までと同じ演算
        output.color = gMaterial.color * textureColor;
    }

    // エミッション(自己発光)。Emissionチェックが付いたマテリアルだけ、
    // ライティングの有無に関わらず加算する(αには影響させない)。
    if (gMaterial.emissiveEnabled != 0)
    {
        // エミッションマップ(t2)を乗算する。**マップ未指定のマテリアルには白1x1が入る**ので、
        // ここに分岐は要らない(白=1倍=マップ無しと同じ)。黒い箇所は光らず、白い箇所だけが光る。
        float32_t3 emissiveMask = SampleTexture(gEmissiveTexture, transformedUV.xy).rgb;
        float32_t3 emissive = gMaterial.emissiveColor * gMaterial.emissiveIntensity * emissiveMask;
        output.color.rgb += emissive;

        // 露出光(ブルーム)へ回す成分。マテリアル別の閾値(soft knee付き)で絞り、滲み強度を掛けて
        // エミッション専用RTへ書く。ブルームチェーンはこのRTだけを入力にする。
        float32_t brightness = max(emissive.r, max(emissive.g, emissive.b));
        float32_t knee = gMaterial.bloomThreshold * gMaterial.bloomSoftKnee;
        float32_t soft = clamp(brightness - gMaterial.bloomThreshold + knee, 0.0f, 2.0f * knee);
        soft = soft * soft / (4.0f * knee + 1e-4f);
        float32_t contribution = max(soft, brightness - gMaterial.bloomThreshold) / max(brightness, 1e-4f);
        // FLOAT16/R11G11B10の飽和対策クランプ。
        output.emission.rgb = min(emissive * contribution * gMaterial.bloomIntensity, 65000.0f);
    }

    if (textureColor.a <= 0.5)
    {
        discard;
    }
    if (textureColor.a == 0.0)
    {
        discard;
    }
    if (output.color.a == 0.0)
    {
        discard;
    }
    
    return output;
}
