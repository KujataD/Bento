#include "PostEffect.hlsli"

// 距離+高さフォグ、SpotLightによる霧の色付け(inscattering)。
// 深度からワールド座標を復元し、カメラ→ピクセルのレイに沿ってレイマーチする。
// HDRシーン(リニア)へ適用し、HDRのまま出力する(トーンマップより前段)。

Texture2D<float32_t4> gScene : register(t0); // HDRシーン(リニア)
Texture2D<float32_t> gDepth : register(t1);  // 深度(R24_UNORM、0=近 1=遠)
SamplerState gSampler : register(s0);

// C++側 FogConstants(postprocess/PostEffectPipeline.h)と完全に一致させること(ルート定数で転送される)。
struct FogConstants
{
    float32_t4x4 invViewProjection; // ビュープロジェクション逆行列(深度→ワールド座標復元用)
    float32_t3 cameraWorldPosition;
    float32_t enabled;        // 0/1
    float32_t3 fogColor;
    float32_t density;        // 距離ベースの指数フォグ密度係数
    float32_t heightFalloff;  // 高さによる密度減衰(0で高さ無関係の均一フォグ)
    float32_t heightBase;     // この高さより上ほど霧が薄くなる
    float32_t startDistance;  // このカメラ距離までフォグを効かせない
    float32_t maxDistance;    // レイマーチの最大距離(遠景クランプ)
    float32_t spotScatter;    // スポットライトが霧を照らす強さ(演出用係数)
    float32_t3 padding;
};

ConstantBuffer<FogConstants> gFogCB : register(b1);

// Object3d.PS.hlslと同じSpotLightデータ(3d/SpotLight.hのSpotLightData)。
// cbuffer配列の1要素は64バイト固定。C++側のstatic_assertと対で守ること。
struct SpotLight
{
    float32_t4 color;
    float32_t3 position;
    float32_t intensity;
    float32_t3 direction;
    float32_t distance;
    float32_t decay;
    float32_t cosAngle;
    float32_t cosFalloffStart;
    float32_t padding;
};

static const uint32_t kMaxSpotLight = 16;
cbuffer gSpotLight : register(b4)
{
    SpotLight spotLights[kMaxSpotLight];
    int32_t spotLightCount;
};

static const int32_t kRaymarchSteps = 24;

// 指定ワールド座標での霧密度(距離減衰は含まない。高さのみ)。
float32_t FogDensityAt(float32_t3 worldPos)
{
    float32_t heightTerm = exp(-gFogCB.heightFalloff * max(worldPos.y - gFogCB.heightBase, 0.0f));
    return gFogCB.density * heightTerm;
}

// 指定ワールド座標に届く全SpotLightの放射輝度の合計(コーン減衰+距離減衰)。
// Object3d.PS.hlslのSpotLight処理と同じ式(表面の法線項は霧には無いので除く)。
float32_t3 SpotLightRadianceAt(float32_t3 worldPos)
{
    float32_t3 radiance = float32_t3(0.0f, 0.0f, 0.0f);

    for (int32_t s = 0; s < spotLightCount; s++)
    {
        if (spotLights[s].intensity <= 0.0f)
        {
            continue;
        }
        float32_t3 toSample = worldPos - spotLights[s].position;
        float32_t distanceToLight = length(toSample);
        if (distanceToLight >= spotLights[s].distance || distanceToLight < 1e-4f)
        {
            continue;
        }
        float32_t3 lightToSampleDir = toSample / distanceToLight;
        float32_t cosAngle = dot(lightToSampleDir, normalize(spotLights[s].direction));
        float32_t falloffFactor = saturate((cosAngle - spotLights[s].cosAngle) / (spotLights[s].cosFalloffStart - spotLights[s].cosAngle));
        float32_t attenuationFactor = pow(saturate(-distanceToLight / spotLights[s].distance + 1.0f), spotLights[s].decay);
        radiance += spotLights[s].color.rgb * spotLights[s].intensity * attenuationFactor * falloffFactor;
    }

    return radiance;
}

float32_t4 main(FullscreenVSOutput input) : SV_TARGET0
{
    float32_t3 sceneColor = gScene.Sample(gSampler, input.texcoord).rgb;
    if (gFogCB.enabled < 0.5f)
    {
        return float32_t4(sceneColor, 1.0f);
    }

    // 深度→ワールド座標復元。深度1.0(何も描かれていない空)はmaxDistanceの遠景として扱う。
    float32_t depth = gDepth.Sample(gSampler, input.texcoord);
    float32_t2 ndcXY = float32_t2(input.texcoord.x * 2.0f - 1.0f, 1.0f - input.texcoord.y * 2.0f);
    float32_t4 clipPos = float32_t4(ndcXY, depth, 1.0f);
    float32_t4 worldPosH = mul(clipPos, gFogCB.invViewProjection);
    float32_t3 worldPos = worldPosH.xyz / max(worldPosH.w, 1e-6f);

    float32_t3 toPixel = worldPos - gFogCB.cameraWorldPosition;
    float32_t pixelDistance = length(toPixel);
    if (depth >= 1.0f - 1e-6f)
    {
        pixelDistance = gFogCB.maxDistance;
    }
    float32_t3 rayDir = toPixel / max(pixelDistance, 1e-4f);

    // startDistance〜min(ピクセル距離, maxDistance)の区間をレイマーチ。
    float32_t marchEnd = min(pixelDistance, gFogCB.maxDistance);
    float32_t marchStart = min(gFogCB.startDistance, marchEnd);
    float32_t marchLength = marchEnd - marchStart;
    if (marchLength <= 1e-4f)
    {
        return float32_t4(sceneColor, 1.0f);
    }

    float32_t stepLength = marchLength / float32_t(kRaymarchSteps);
    float32_t transmittance = 1.0f;
    float32_t3 inscatter = float32_t3(0.0f, 0.0f, 0.0f);

    for (int32_t i = 0; i < kRaymarchSteps; ++i)
    {
        // 各ステップの中点でサンプリング(数値誤差でのバンディングを軽減)。
        float32_t t = marchStart + (float32_t(i) + 0.5f) * stepLength;
        float32_t3 samplePos = gFogCB.cameraWorldPosition + rayDir * t;

        float32_t sampleDensity = FogDensityAt(samplePos);
        if (sampleDensity <= 0.0f)
        {
            continue;
        }
        float32_t stepOpticalDepth = sampleDensity * stepLength;

        // 散乱光 = 環境の霧色 + スポットライトの寄与(コーン内なら霧がライト色に染まる)。
        float32_t3 scatteredLight = gFogCB.fogColor + SpotLightRadianceAt(samplePos) * gFogCB.spotScatter;

        // Beer-Lambert。ステップ内での自己減衰も考慮した解析的積分(energy-conserving)。
        float32_t stepTransmittance = exp(-stepOpticalDepth);
        inscatter += transmittance * (1.0f - stepTransmittance) * scatteredLight;
        transmittance *= stepTransmittance;
    }

    float32_t3 result = sceneColor * transmittance + inscatter;
    return float32_t4(result, 1.0f);
}
