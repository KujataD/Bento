#include "PostEffect.hlsli"

Texture2D<float32_t4> gScene : register(t0); // HDRシーン(リニア)
Texture2D<float32_t4> gBloom : register(t1); // ブルーム(bloomIntensity=0なら未使用扱い)
SamplerState gSampler : register(s0);

// ブルーム合成 → 露出 → トーンマップ → 簡易グレーディング → ビネット → フェード。
// HDR(リニア)からLDRへの最終変換パス。出力先RTVがsRGBなのでリニア値をそのまま返す。
float32_t4 main(FullscreenVSOutput input) : SV_TARGET0
{
    float32_t3 color = gScene.Sample(gSampler, input.texcoord).rgb;

    // ブルーム合成(リニアHDR空間で加算)。
    color += gBloom.Sample(gSampler, input.texcoord).rgb * gPost.bloomIntensity;

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
    float32_t2 centered = input.texcoord - 0.5f;
    float32_t vignette = 1.0f - smoothstep(0.4f, 0.4f + max(gPost.vignetteSmoothness, 0.001f), length(centered)) * gPost.vignetteIntensity;
    color *= vignette;

    // 画面フェード(シーン遷移用)。amount=0で恒等。
    color = lerp(color, gPost.fade.rgb, gPost.fade.w);

    return float32_t4(saturate(color), 1.0f);
}
