#include "PostEffect.hlsli"

Texture2D<float32_t4> gScene : register(t0); // HDRシーン(リニア)
Texture2D<float32_t4> gBloom : register(t1); // ブルーム(bloomIntensity=0なら未使用扱い)
SamplerState gSampler : register(s0);

// ブルーム合成 → 露出 → トーンマップ → 簡易グレーディング → ビネット → フェード。
// HDR(リニア)からLDRへの最終変換パス。出力先RTVがsRGBなのでリニア値をそのまま返す。
float32_t4 main(FullscreenVSOutput input) : SV_TARGET0
{
    // シーンの1ピクセルの中心へ寄せたUV。出力のほうが大きい(ドット絵化)ときも、ぼかさずに拡大される。
    // ブルームとビネットもこのUVで読むので、1ドットの中は同じ色になる(ドットの中で明るさが変わらない)。
    // 出力とシーンが同じ大きさなら、ピクセルの中心そのもの(今までと同じ結果)。
    uint32_t sceneWidth;
    uint32_t sceneHeight;
    gScene.GetDimensions(sceneWidth, sceneHeight);
    float32_t2 sceneSize = float32_t2(sceneWidth, sceneHeight);
    float32_t2 uv = (floor(input.texcoord * sceneSize) + 0.5f) / sceneSize;

    float32_t3 color = gScene.Load(int32_t3(int32_t2(uv * sceneSize), 0)).rgb;

    // ブルーム合成(リニアHDR空間で加算)。
    color += gBloom.Sample(gSampler, uv).rgb * gPost.bloomIntensity;

    // 露出調整。
    color *= gPost.exposure;

    // トーンマップ(HDR→LDR)。None(0)は従来描画と数値一致するためHDR化のパリティ検証に使う。
    if (gPost.tonemapType == 1)
    {
        color = TonemapReinhard(color);
    }
    else if (gPost.tonemapType == 2)
    {
        color = TonemapACES(color);
    }
    else
    {
        color = saturate(color);
    }

    // 簡易カラーグレーディング(LDR空間)。デフォルト値(filter=1/sat=1/contrast=1)で恒等。
    color *= gPost.colorFilter.rgb;
    float32_t luminance = dot(color, float32_t3(0.2126f, 0.7152f, 0.0722f));
    color = lerp(float32_t3(luminance, luminance, luminance), color, gPost.saturation);
    color = (color - 0.5f) * gPost.contrast + 0.5f;

    // ビネット(画面端を暗くする)。intensity=0で恒等。
    float32_t2 centered = uv - 0.5f;
    float32_t vignette = 1.0f - smoothstep(0.4f, 0.4f + max(gPost.vignetteSmoothness, 0.001f), length(centered)) * gPost.vignetteIntensity;
    color *= vignette;

    // 画面フェード(シーン遷移用)。amount=0で恒等。
    color = lerp(color, gPost.fade.rgb, gPost.fade.w);

    return float32_t4(saturate(color), 1.0f);
}
