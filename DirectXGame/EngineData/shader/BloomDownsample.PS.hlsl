#include "PostEffect.hlsli"

Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// 13タップダウンサンプル(CoD:AW式)。1/2解像度へ落とす。
// texelSizeは入力(1段上の)テクスチャの1テクセル。
float32_t4 main(FullscreenVSOutput input) : SV_TARGET0
{
    float32_t2 uv = input.texcoord;
    float32_t2 ts = gPost.texelSize;

    float32_t3 a = gTexture.Sample(gSampler, uv + ts * float32_t2(-2.0f, -2.0f)).rgb;
    float32_t3 b = gTexture.Sample(gSampler, uv + ts * float32_t2(0.0f, -2.0f)).rgb;
    float32_t3 c = gTexture.Sample(gSampler, uv + ts * float32_t2(2.0f, -2.0f)).rgb;
    float32_t3 d = gTexture.Sample(gSampler, uv + ts * float32_t2(-2.0f, 0.0f)).rgb;
    float32_t3 e = gTexture.Sample(gSampler, uv).rgb;
    float32_t3 f = gTexture.Sample(gSampler, uv + ts * float32_t2(2.0f, 0.0f)).rgb;
    float32_t3 g = gTexture.Sample(gSampler, uv + ts * float32_t2(-2.0f, 2.0f)).rgb;
    float32_t3 h = gTexture.Sample(gSampler, uv + ts * float32_t2(0.0f, 2.0f)).rgb;
    float32_t3 i = gTexture.Sample(gSampler, uv + ts * float32_t2(2.0f, 2.0f)).rgb;
    float32_t3 j = gTexture.Sample(gSampler, uv + ts * float32_t2(-1.0f, -1.0f)).rgb;
    float32_t3 k = gTexture.Sample(gSampler, uv + ts * float32_t2(1.0f, -1.0f)).rgb;
    float32_t3 l = gTexture.Sample(gSampler, uv + ts * float32_t2(-1.0f, 1.0f)).rgb;
    float32_t3 m = gTexture.Sample(gSampler, uv + ts * float32_t2(1.0f, 1.0f)).rgb;

    float32_t3 color = e * 0.125f;
    color += (a + c + g + i) * 0.03125f;
    color += (b + d + f + h) * 0.0625f;
    color += (j + k + l + m) * 0.125f;
    return float32_t4(color, 1.0f);
}
