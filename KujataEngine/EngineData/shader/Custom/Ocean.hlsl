// エンジン同梱の海(OceanComponent が使う)。風のタクト / A Short Hike 風の、ローポリ・ドット絵向けの海。
//   - 平らな海の色(陰は Lighting の分だけ)に、ワールド座標に貼った泡の模様(輪)と、少しずらした暗い輪を重ねる
//   - 模様を読む位置を sin を重ねた式で揺らし、ゆっくり流す
//   - 奥の不透明物までの距離(gSceneDepth)で、浅瀬の色・透け具合・岸の泡を決める
//   - 頂点は正弦波を最大4つ足して上下させ、波の山を白くする
//
// 波は **OceanComponent::EvaluateWaves(C++)と同じ式**にしておくこと。ずれると、浮かぶ物や水弾が海面の高さと合わなくなる。
//
// 値はすべて OceanComponent が gShaderParams.objectParams で渡す(マテリアルの Shader Params は使わない):
//   [0..3] 波 (向き[度], 波長[m], 高さ[m], 速さ[m/s])。波長が0の波は使わない
//   [4] 沖の色 rgb, 陰の強さ(0=べた塗り 1=トゥーンの陰そのまま)
//   [5] 浅瀬の色 rgb, 浅瀬の色になる深さ[m](0 で深さを使わない)
//   [6] 泡の色 rgb, 岸の泡の幅[m](0 で岸の泡なし)
//   [7] 暗い輪の色 rgb, 波の山の泡が出る高さ(0〜1。0 で山の泡なし)
//   [8] 模様の大きさ[m], 泡の量(0〜1), 輪の太さ(0〜1), 暗い輪のずれ[m]
//   [9] 揺らぎの強さ[m], 揺らぎの波長[m], 流れる速さ[m/s], 模様にテクスチャを使うか(1=使う)
//   [10] 海の基準の高さ(ワールドの Y), 浅瀬の不透明度(0〜1), 流れる向き[度], (未使用)
#include "Object3dCustom.hlsli"

#define OCEAN_PARAM(i) gShaderParams.objectParams[i]

// 位置 xz[m] の波の高さ。maxHeight には波の高さの合計(山の最大)を返す。
float32_t WaveHeight(float32_t2 xz, out float32_t maxHeight)
{
    float32_t height = 0.0f;
    maxHeight = 0.0f;
    [unroll]
    for (int32_t i = 0; i < 4; ++i)
    {
        const float32_t4 wave = OCEAN_PARAM(i);
        if (wave.y <= 0.0f)
        {
            continue;
        }
        const float32_t angle = radians(wave.x);
        const float32_t2 direction = float32_t2(cos(angle), sin(angle));
        const float32_t k = 6.2831853f / wave.y;
        height += wave.z * sin(k * (dot(direction, xz) - wave.w * gShaderParams.time));
        maxHeight += abs(wave.z);
    }
    return height;
}

#ifdef KUJATA_VERTEX_SHADER
VertexShaderOutput VSMain(VertexShaderInput input)
{
    // 波はワールド座標の xz で決める(海の板が動いても、波はその場に留まる)。
    const float32_t3 world = mul(input.position, gTranformationMatrix.World).xyz;
    float32_t maxHeight;
    input.position.y += WaveHeight(world.xz, maxHeight);
    return DefaultVertex(input);
}
#endif

#ifdef KUJATA_PIXEL_SHADER
float32_t2 Hash2(float32_t2 p)
{
    p = float32_t2(dot(p, float32_t2(127.1f, 311.7f)), dot(p, float32_t2(269.5f, 183.3f)));
    return frac(sin(p) * 43758.5453f);
}

float32_t Hash1(float32_t2 p)
{
    return frac(sin(dot(p, float32_t2(41.3f, 289.1f))) * 15731.743f);
}

// 泡の輪の模様(風のタクトの輪)。p は模様のマス単位の位置。輪の上なら 1。
// マスごとに乱数で「輪があるか・中心・半径」を決め、周り 3×3 マスの輪を調べる(輪がマスの外へはみ出してもよいように)。
float32_t FoamRings(float32_t2 p, float32_t amount, float32_t width)
{
    const float32_t2 cell = floor(p);
    const float32_t2 local = p - cell;
    float32_t nearest = 1.0e4f;
    [unroll]
    for (int32_t y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int32_t x = -1; x <= 1; ++x)
        {
            const float32_t2 neighbor = float32_t2(x, y);
            const float32_t2 id = cell + neighbor;
            if (Hash1(id) > amount)
            {
                continue;
            }
            const float32_t2 center = neighbor + 0.2f + 0.6f * Hash2(id);
            const float32_t radius = 0.22f + 0.2f * Hash1(id + 17.0f);
            nearest = min(nearest, abs(length(local - center) - radius));
        }
    }
    return nearest < width * 0.5f ? 1.0f : 0.0f;
}

// 模様(輪、またはマテリアルのテクスチャ)。xz はワールド座標[m]。
float32_t FoamPattern(float32_t2 xz)
{
    const float32_t4 pattern = OCEAN_PARAM(8);
    const float32_t cellSize = max(pattern.x, 0.01f);
    if (OCEAN_PARAM(9).w > 0.5f)
    {
        // テクスチャの赤が半分より明るいところを泡にする(1枚が模様の大きさ[m]に広がる)。
        return SampleTexture(gTexture, xz / cellSize).r > 0.5f ? 1.0f : 0.0f;
    }
    return FoamRings(xz / cellSize, pattern.y, pattern.z);
}

PixelShaderOutput PSMain(VertexShaderOutput input)
{
    const float32_t time = gShaderParams.time;
    const float32_t2 worldXZ = input.worldPosition.xz;

    // --- 深さ(奥の不透明物までの距離)で、浅瀬の色と透け具合を段で切り替える ---
    const float32_t behind = SceneDepthBehind(input.position);
    const float32_t depthColorDistance = OCEAN_PARAM(5).w;
    float32_t deep = 1.0f;
    if (depthColorDistance > 0.0f)
    {
        deep = floor(saturate(behind / depthColorDistance) * 3.0f) / 3.0f; // 0 / 0.33 / 0.67 / 1 の4段
    }
    float32_t3 color = lerp(OCEAN_PARAM(5).rgb, OCEAN_PARAM(4).rgb, deep);
    float32_t alpha = lerp(OCEAN_PARAM(10).y, 1.0f, deep);

    // --- 陰(トゥーン)。Lighting が 0 ならべた塗り(風のタクト)、1 なら段の陰そのまま ---
    const float32_t lighting = OCEAN_PARAM(4).w;
    const float32_t3 normal = (gMaterial.flatShading != 0) ? FlatNormal(input.worldPosition) : normalize(input.normal);
    const float32_t lit = ToonStep(saturate(dot(normal, -normalize(gDirectionalLight.direction))));
    const float32_t3 shadow = color * gDirectionalLight.shadowColor;
    const float32_t3 bright = max(color * gDirectionalLight.color.rgb * gDirectionalLight.intensity, shadow);
    color = lerp(color, lerp(shadow, bright, lit), lighting);

    // 波のどのあたりか(0=いちばん低い谷 〜 1=いちばん高い山)。頂点の高さを補間した値(= 描かれている面の高さ)なので、
    // 三角形の中で直線的に変わり、境目が面に沿ってカクカクになる。
    float32_t maxHeight;
    WaveHeight(worldXZ, maxHeight);
    const float32_t crest = maxHeight > 0.0f ? saturate((input.worldPosition.y - OCEAN_PARAM(10).x) / maxHeight * 0.5f + 0.5f) : 0.5f;
    if (crest < 0.3f)
    {
        color *= lerp(1.0f, 0.82f, lighting); // 谷を1段だけ濃く(陰が弱いほど控えめに)
    }

    // --- 泡の模様。読む位置を sin を重ねて揺らし、ゆっくり流す ---
    const float32_t4 distort = OCEAN_PARAM(9);
    float32_t2 patternXZ = worldXZ;
    if (distort.x > 0.0f && distort.y > 0.0f)
    {
        const float32_t k = 6.2831853f / distort.y;
        const float32_t2 wobble = float32_t2(
            sin(worldXZ.y * k + time * 1.3f) + 0.5f * sin(worldXZ.y * k * 1.7f - time * 0.9f + 1.3f),
            sin(worldXZ.x * k + time * 1.1f) + 0.5f * sin(worldXZ.x * k * 2.3f + time * 0.7f + 2.1f));
        patternXZ += wobble * (distort.x / 1.5f);
    }
    const float32_t driftAngle = radians(OCEAN_PARAM(10).z);
    patternXZ -= float32_t2(cos(driftAngle), sin(driftAngle)) * distort.z * time;

    const float32_t3 foamColor = OCEAN_PARAM(6).rgb;
    if (OCEAN_PARAM(8).x > 0.0f && OCEAN_PARAM(8).y > 0.0f)
    {
        // 暗い輪: 同じ模様を少しずらして、泡の下に敷く(泡が浮いて見える)。
        const float32_t ringOffset = OCEAN_PARAM(8).w;
        if (ringOffset > 0.0f && FoamPattern(patternXZ + float32_t2(ringOffset, ringOffset * 0.6f)) > 0.5f)
        {
            color = OCEAN_PARAM(7).rgb;
        }
        if (FoamPattern(patternXZ) > 0.5f)
        {
            color = foamColor;
        }
    }

    // --- 波の山の泡 ---
    const float32_t crestFoam = OCEAN_PARAM(7).w;
    if (crestFoam > 0.0f && crest > crestFoam)
    {
        color = foamColor;
    }

    // --- 岸の泡(奥の物が近いところ)。幅を少しずつ揺らして、打ち寄せて見せる ---
    const float32_t shoreFoam = OCEAN_PARAM(6).w;
    if (shoreFoam > 0.0f)
    {
        const float32_t lap = shoreFoam * (0.75f + 0.25f * sin(time * 2.0f + dot(worldXZ, float32_t2(0.35f, 0.2f))));
        if (behind < lap)
        {
            color = foamColor;
            alpha = 1.0f;
        }
    }

    PixelShaderOutput output;
    output.color = float32_t4(color * gMaterial.color.rgb, alpha * gMaterial.color.a);
    output.emission = float32_t4(0.0f, 0.0f, 0.0f, 1.0f);
    return output;
}
#endif
