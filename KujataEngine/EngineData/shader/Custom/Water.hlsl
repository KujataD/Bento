// エンジン同梱の水流(WaterSprayComponent が SplineRendererComponent の Tube に使う)。
// ローポリ・ドット絵に合わせて、なめらかなグラデーションは使わず、すべて「段」で切り替える:
//   陰(段の数を自分で決める) / 白い縁取り / 流れる白い泡の筋 / 先端が面ごとに欠けて消える / 太さのうねり
//
// 値は **WaterSprayComponent が gShaderParams.objectParams で渡す**ので、マテリアルを作らなくても使える
// (マテリアルを設定した場合は、その色が掛け算され、テクスチャも乗る)。
//   [0] うねり: x=大きさ(太さに対する割合) y=間隔(1mあたり) z=流れる速さ[m/s] w=断面の角の数
//   [1] 泡の筋: x=間隔(1mあたり) y=流れる速さ[m/s] z=筋の長さ(0〜1。0で筋なし) w=縁取りの太さ(0〜1)
//   [2] 泡・筋の色 rgb, w=先端が欠け始める位置(0〜1。0.7 なら先端から3割)
//   [3] 縁取りの色 rgb, w=欠けるブロックの細かさ(1mあたり)
//   [4] 水の色 rgb, w=不透明度
//   [5] x=陰の段の数(2以上) y=面ごとに平らな陰にするか(1でする) z,w=未使用
//
// UV は SplineRenderer のもの。**SplineRenderer の UV Per Unit は 1 にする**(u = 根元からの距離[m])。
// 模様を距離で決めるので、水流が短くなっても流れる速さと間隔が変わらない。
// 根元0〜先端1の位置は u / gShaderParams.curveLength(SplineRenderer が曲線の全長を渡す)。v = 断面の周 0〜1。
//
// gShaderParams.userValue(WaterSprayComponent が水流ごとに渡す): 1 = 先端が物に当たって止まっている。
//   このときは先端を欠けさせない(当たった所に穴が空いて見えるため)。欠けるのは、何にも当たらず飛んでいく先端だけ。
#include "Object3dCustom.hlsli"

#define WATER_PARAM(i) gShaderParams.objectParams[i]

// 根元0〜先端1 の位置(SplineRenderer 以外で使われたときは u をそのまま使う)。
float32_t CurveRatio(float32_t u)
{
    return gShaderParams.curveLength > 0.0f ? saturate(u / gShaderParams.curveLength) : u;
}

// 格子のマスごとに決まる 0〜1 の値(毎フレーム同じマスなら同じ値)。
float32_t Hash(float32_t2 cell)
{
    return frac(sin(dot(cell, float32_t2(12.9898f, 78.233f))) * 43758.5453f);
}

#ifdef KUJATA_VERTEX_SHADER
// 太さを長さ方向に波打たせて、輪郭をまっすぐでなくする(ホースに見えないように)。
VertexShaderOutput VSMain(VertexShaderInput input)
{
    const float32_t amount = WATER_PARAM(0).x;
    const float32_t wavesPerMeter = WATER_PARAM(0).y;
    const float32_t speed = WATER_PARAM(0).z;
    const float32_t u = input.texcoord.x;
    // 波の形は距離で決め(u[m] × 1mあたりの数)、根元は揺らさず先へ行くほど大きく揺らす。
    const float32_t wave = sin((u - gShaderParams.time * speed) * wavesPerMeter * 6.2831853f) * amount * CurveRatio(u);
    // 断面の頂点は、法線 = 中心から外への向き。その向きへ押し出す(ふたの頂点は先端の向きなので、少し伸び縮みするだけ)。
    input.position.xyz += input.normal * wave;
    return DefaultVertex(input);
}
#endif

#ifdef KUJATA_PIXEL_SHADER
PixelShaderOutput PSMain(VertexShaderOutput input)
{
    const float32_t u = input.texcoord.x;
    const float32_t v = input.texcoord.y;
    const float32_t sides = max(WATER_PARAM(0).w, 3.0f);
    // 面の番号(断面の何面目か)。泡と欠け方を面ごとにそろえて、ローポリの面がそのまま模様の単位になるようにする。
    const float32_t face = floor(v * sides);

    // --- 先端が面ごとに欠けて消える ---
    // 欠けるかどうかは先端からの位置(0〜1)で、ブロックの区切りは距離で決める(長さが変わってもブロックが伸び縮みしない)。
    const float32_t ratio = CurveRatio(u);
    const float32_t dissolveStart = WATER_PARAM(2).w;
    const bool tipAttached = gShaderParams.userValue > 0.5f;
    if (!tipAttached && dissolveStart > 0.0f && ratio > dissolveStart)
    {
        const float32_t cellsPerMeter = max(WATER_PARAM(3).w, 0.1f);
        const float32_t amount = (ratio - dissolveStart) / max(1.0f - dissolveStart, 1e-3f);
        if (Hash(float32_t2(floor(u * cellsPerMeter), face)) < amount)
        {
            discard;
        }
    }

    // --- 陰(段の数はコンポーネントの設定。影の色は世界共通) ---
    const bool flat = WATER_PARAM(5).y > 0.5f;
    const float32_t3 normal = flat ? FlatNormal(input.worldPosition) : normalize(input.normal);
    const float32_t4 textureColor = SampleTexture(gTexture, input.texcoord);
    const float32_t3 albedo = WATER_PARAM(4).rgb * gMaterial.color.rgb * textureColor.rgb;
    const float32_t steps = max(WATER_PARAM(5).x, 2.0f);
    const float32_t brightness = saturate(dot(normal, -normalize(gDirectionalLight.direction)));
    const float32_t lit = min(floor(brightness * steps), steps - 1.0f) / (steps - 1.0f);
    const float32_t3 shadow = albedo * gDirectionalLight.shadowColor;
    const float32_t3 bright = max(albedo * gDirectionalLight.color.rgb * gDirectionalLight.intensity, shadow);
    float32_t3 color = lerp(shadow, bright, lit);

    // --- 流れる泡の筋(面ごとにずらした白い短い線が、根元から先へ流れる) ---
    // 筋は「根元からの距離 − 速さ×時間」で決めるので、水流の長さによらず同じ速さ[m/s]で流れる。
    const float32_t stripesPerMeter = WATER_PARAM(1).x;
    const float32_t flowSpeed = WATER_PARAM(1).y;
    const float32_t stripeLength = WATER_PARAM(1).z;
    if (stripeLength > 0.0f && stripesPerMeter > 0.0f)
    {
        const float32_t stripe = frac((u - gShaderParams.time * flowSpeed) * stripesPerMeter + Hash(float32_t2(face, 3.0f)));
        if (stripe < stripeLength)
        {
            color = WATER_PARAM(2).rgb;
        }
    }

    // --- 縁取り(視線とほぼ平行な面 = 輪郭のあたりを、1段だけ白くする) ---
    const float32_t rimWidth = WATER_PARAM(1).w;
    if (rimWidth > 0.0f)
    {
        const float32_t3 toEye = normalize(gCamera.worldPosition - input.worldPosition);
        const float32_t rim = 1.0f - saturate(abs(dot(normalize(input.normal), toEye)));
        if (rim > 1.0f - rimWidth)
        {
            color = WATER_PARAM(3).rgb;
        }
    }

    PixelShaderOutput output;
    output.color = float32_t4(color, WATER_PARAM(4).w * gMaterial.color.a * textureColor.a);
    output.emission = float32_t4(0.0f, 0.0f, 0.0f, 1.0f);
    return output;
}
#endif
