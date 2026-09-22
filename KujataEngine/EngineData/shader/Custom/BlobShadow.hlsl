// エンジン同梱の丸影(BlobShadowComponent が使う)。
// 頂点は BlobShadowComponent が地面・海面に沿わせたワールド座標で作るので、頂点シェーダーは標準のまま。
// 法線の x は「この点に影を描くか」(1=描く、0=縁の外)。頂点シェーダーが法線を正規化するので、描く点では約0.7・描かない点では0になる。補間してその中間(0.35)を下回ったところを捨てるので、影が縁で切れる。
// ピクセルシェーダーは UV の中心からの距離で円を描き、外側は捨てる。縁は段で薄くする(ドット絵向けにぼかさない)。
//
// 値は BlobShadowComponent が gShaderParams.objectParams で渡す:
//   [0] 影の色 rgb, 濃さ(0〜1。高さによる薄まりを掛けた後)
//   [1] 段の数(1=くっきりした円 / 2=濃い芯+薄い縁 …), (未使用) ×3
#include "Object3dCustom.hlsli"

#ifdef KUJATA_PIXEL_SHADER
PixelShaderOutput PSMain(VertexShaderOutput input)
{
    // 0=中心 〜 1=縁。
    const float32_t distance = length(input.texcoord - 0.5f) * 2.0f;
    if (distance > 1.0f || input.normal.x < 0.35f)
    {
        discard;
    }
    const float32_t4 shadow = gShaderParams.objectParams[0];
    const float32_t steps = max(gShaderParams.objectParams[1].x, 1.0f);
    // 外へ向かって段ごとに薄くする(1段なら一様な濃さ)。
    const float32_t band = floor(distance * steps);
    const float32_t alpha = shadow.a * (1.0f - band / steps);

    PixelShaderOutput output;
    output.color = float32_t4(shadow.rgb, alpha);
    output.emission = float32_t4(0.0f, 0.0f, 0.0f, 1.0f);
    return output;
}
#endif
