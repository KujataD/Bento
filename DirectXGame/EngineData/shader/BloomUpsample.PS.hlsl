#include "PostEffect.hlsli"

Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// 3x3テントフィルタで1段上の解像度へ広げる。
// PSO側の加算ブレンド(ONE/ONE)で1段上のダウンサンプル結果へ累積し、広く自然なブルームの裾を作る。
// texelSizeは入力(1段下の)テクスチャの1テクセル。
float32_t4 main(FullscreenVSOutput input) : SV_TARGET0
{
    float32_t2 uv = input.texcoord;
    float32_t2 ts = gPost.texelSize;

    float32_t3 color = gTexture.Sample(gSampler, uv + ts * float32_t2(-1.0f, -1.0f)).rgb;
    color += gTexture.Sample(gSampler, uv + ts * float32_t2(0.0f, -1.0f)).rgb * 2.0f;
    color += gTexture.Sample(gSampler, uv + ts * float32_t2(1.0f, -1.0f)).rgb;
    color += gTexture.Sample(gSampler, uv + ts * float32_t2(-1.0f, 0.0f)).rgb * 2.0f;
    color += gTexture.Sample(gSampler, uv).rgb * 4.0f;
    color += gTexture.Sample(gSampler, uv + ts * float32_t2(1.0f, 0.0f)).rgb * 2.0f;
    color += gTexture.Sample(gSampler, uv + ts * float32_t2(-1.0f, 1.0f)).rgb;
    color += gTexture.Sample(gSampler, uv + ts * float32_t2(0.0f, 1.0f)).rgb * 2.0f;
    color += gTexture.Sample(gSampler, uv + ts * float32_t2(1.0f, 1.0f)).rgb;
    return float32_t4(color * (1.0f / 16.0f), 1.0f);
}
