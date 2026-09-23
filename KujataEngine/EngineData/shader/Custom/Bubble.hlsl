// エンジン同梱の泡(シャボン玉)。ローポリ・ドット絵向けに、薄膜の虹色を「段」で塗り分ける。
// テクスチャは使わず、**その面をどの角度から見ているか**だけで色を決めるので、カメラや泡が動くと色が流れる(プリズム感)。
// 屈折はしない(ドット絵では伝わらないため)。中を抜いて膜だけ描くので、半透明にしなくても向こう側が見える(不透明のままで並び順の問題が出ない)。
//
// 使い方: 球(分割の少ないもの)の ModelRenderer に、このシェーダーを選んだマテリアルを設定するだけ。
// 泡ごとに色を変えたいときは、ModelRendererComponent の Shader User Value(0〜1)をばらばらにする。
//
// マテリアルの Shader Params:
//   Param 0 = (色相の始まり(0〜1), 縁までに色相が回る回数, 段の数(2以上), 縁での鮮やかさ(0〜1。正面は白く、縁へ行くほど色づく))
//   Param 1 = (明るさ, リム(縁の白)の太さ(0〜1), リムの強さ(0〜1), ハイライトの強さ(0〜1))
//   Param 2 = (ハイライトの大きさ(0〜1), 色相が時間で回る速さ(1秒あたり), 下側の暗さ(1=暗くしない), 縁の鋭さ(1で素直))
//   Param 3 = (面の向きで色相が回る量(0〜1くらい), 中を抜く割合(0〜1。0で抜かない、0.5なら内側半分が抜けて膜だけ残る),
//              正面の白さ(0=全面に色がのる / 1=正面は白く縁だけ色づく), (未使用))
// gShaderParams.userValue = この泡だけの色相のずれ(0〜1)。ModelRendererComponent の Shader User Value が入る。
#include "Object3dCustom.hlsli"

#ifdef KUJATA_PIXEL_SHADER
// 色相(0〜1)・鮮やかさ・明るさ から RGB を作る。
float32_t3 HsvToRgb(float32_t hue, float32_t saturation, float32_t value)
{
    const float32_t3 k = float32_t3(1.0f, 2.0f / 3.0f, 1.0f / 3.0f);
    const float32_t3 p = abs(frac(hue + k) * 6.0f - 3.0f);
    return value * lerp(float32_t3(1.0f, 1.0f, 1.0f), saturate(p - 1.0f), saturation);
}

PixelShaderOutput PSMain(VertexShaderOutput input)
{
    const float32_t4 hueParams = gShaderParams.params[0];
    const float32_t4 lookParams = gShaderParams.params[1];
    const float32_t4 extraParams = gShaderParams.params[2];

    // 面ごとに平らな向き(Flat Shading)なら、ローポリの面ごとに色が切り替わる。
    const float32_t3 normal = (gMaterial.flatShading != 0) ? FlatNormal(input.worldPosition) : normalize(input.normal);
    const float32_t3 toEye = normalize(gCamera.worldPosition - input.worldPosition);

    // 正面は0、縁へ行くほど1。面の向きと視線の「傾き」(sin)を使うと、画面で見たときの半径とほぼ同じになり、
    // 帯が同心円に等間隔で並ぶ(1 - cos だと縁の直前まで小さいままで、中が白いだけになる)。
    const float32_t facing = saturate(dot(normal, toEye));
    const float32_t edgeSharpness = max(extraParams.w, 0.01f);
    const float32_t fresnel = pow(saturate(sqrt(saturate(1.0f - facing * facing))), edgeSharpness);

    // 角度を段に分ける(0=正面 〜 1=縁)。この段が、色相と「どれだけ色づくか」の両方を決める。
    // 実物のシャボン玉と同じで、正面はほとんど白く、縁へ行くほど色が濃くなる。
    // 段は 0(正面)〜1(いちばん外)になるように割る。steps で割ると最大が 1 に届かず、いちばん外の帯が淡いままになる。
    const float32_t steps = max(hueParams.z, 2.0f);
    const float32_t band = min(floor(fresnel * steps), steps - 1.0f) / (steps - 1.0f);
    // 面の向き(ライト側を向いているか)でも色相をずらす。泡が動く・回ると、帯の色が移って見える。
    const float32_t3 toLight = -normalize(gDirectionalLight.direction);
    const float32_t directional = dot(normal, toLight) * 0.5f + 0.5f;
    // 色相も段で丸める(そのままだと色がにじんで、ローポリ・ドット絵の中で浮く)。
    const float32_t hueRaw = frac(hueParams.x + band * hueParams.y + directional * gShaderParams.params[3].x + gShaderParams.userValue +
                                  gShaderParams.time * extraParams.y);
    const float32_t hue = floor(hueRaw * steps) / steps;
    // 「正面をどれだけ白くするか」(0=全面に同じ濃さで色がのる / 1=正面は白く、縁だけ色づく)。
    const float32_t centerWhite = saturate(gShaderParams.params[3].z);
    const float32_t colorAmount = lerp(1.0f, band, centerWhite);
    float32_t3 color = HsvToRgb(hue, saturate(hueParams.w) * colorAmount, max(lookParams.x, 0.0f));

    // 中を抜く(泡の膜だけを残す)。半透明にしなくても向こう側が見えるので、ドットも描く順番もきれいなまま。
    // ハイライトの点は中に浮かせたいので、抜く前に判定して残す。
    const float32_t3 toLightVector = -normalize(gDirectionalLight.direction);
    const float32_t specular = saturate(dot(normal, normalize(toLightVector + toEye)));
    const float32_t highlightSize = saturate(extraParams.x);
    const bool inHighlight = highlightSize > 0.0f && specular > 1.0f - highlightSize * 0.5f;
    const float32_t hole = saturate(gShaderParams.params[3].y);
    if (!inHighlight && band < hole)
    {
        discard;
    }

    // 下を向いた面を1段だけ暗くする(球らしさを出す。1なら暗くしない)。
    color *= lerp(1.0f, extraParams.z, step(0.35f, saturate(-normal.y)));

    // リム: いちばん縁の段を白くする(泡らしさの決め手)。
    const float32_t rimWidth = saturate(lookParams.y);
    if (rimWidth > 0.0f && band > 1.0f - rimWidth)
    {
        color = lerp(color, float32_t3(1.0f, 1.0f, 1.0f), saturate(lookParams.z));
    }

    // ハイライト: ライト側にできる小さな光の点。
    if (inHighlight)
    {
        color = lerp(color, float32_t3(1.0f, 1.0f, 1.0f), saturate(lookParams.w));
    }

    PixelShaderOutput output;
    output.color = float32_t4(color * gMaterial.color.rgb, gMaterial.color.a);
    // マテリアルの Emission を ON にすると、泡が光って見える(ブルームが乗る)。
    output.emission = (gMaterial.emissiveEnabled != 0) ? float32_t4(color * gMaterial.emissiveColor * gMaterial.emissiveIntensity, 1.0f)
                                                       : float32_t4(0.0f, 0.0f, 0.0f, 1.0f);
    return output;
}
#endif
