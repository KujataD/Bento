// ポストプロセス共通定義。
// フルスクリーン三角形のVS出力と、全ポストパス共有のルート定数、トーンマップ関数を持つ。

struct FullscreenVSOutput
{
    float32_t4 position : SV_POSITION;
    float32_t2 texcoord : TEXCOORD0;
};

// C++側 PostConstants(postprocess/PostEffectPipeline.h)と完全に一致させること(ルート定数で転送される)。
// 閾値/soft kneeはマテリアル別に移行した(Object3d.PSがエミッションRTへ書く時点で適用済み)。
struct PostConstants
{
    float32_t2 texelSize;         // 入力テクスチャの1テクセルサイズ(ダウン/アップサンプル用)
    float32_t bloomIntensity;     // ブルーム合成の全体強度(0で寄与なし)
    float32_t exposure;           // 露出(トーンマップ前の輝度スケール)
    int32_t tonemapType;          // 0=None(saturateのみ) / 1=Reinhard / 2=ACES
    float32_t3 padding0;
    float32_t4 fade;              // rgb=フェード色 / w=適用量(0で無効)
    float32_t saturation;         // 彩度(1=そのまま / 0=グレースケール)
    float32_t contrast;           // コントラスト(1=そのまま)
    float32_t vignetteIntensity;  // ビネット強度(0で無効)
    float32_t vignetteSmoothness; // ビネットの減衰の滑らかさ
    float32_t4 colorFilter;       // 乗算カラーフィルタ(rgbのみ使用)
};

ConstantBuffer<PostConstants> gPost : register(b0);

// ACES(Narkowicz近似)。高輝度が白へ滑らかにロールオフするフィルミックカーブ。
float32_t3 TonemapACES(float32_t3 color)
{
    const float32_t a = 2.51f;
    const float32_t b = 0.03f;
    const float32_t c = 2.43f;
    const float32_t d = 0.59f;
    const float32_t e = 0.14f;
    return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
}

float32_t3 TonemapReinhard(float32_t3 color)
{
    return color / (1.0f + color);
}
