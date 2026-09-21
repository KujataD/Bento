#include "Object3dPixel.hlsli"

// --- バリア(enableLighting == 6)用の定数 ---------------------------------
// 切頂二十面体(サッカーボール)のセル中心方向。
// 中心は「正二十面体の頂点12個(=五角形の中心)」+「正十二面体の頂点20個(=六角形の中心)」の計32個で、
// この集合は原点対称なので、対になる向きは abs(dot) でまとめて16本ぶんだけ持てばよい。
// 法線方向に対して最も近い中心と2番目に近い中心の差からセルの境界を出す(球面ボロノイ)。
static const float32_t3 kBarrierCells[16] =
{
    // 正二十面体の頂点(五角形セルの中心) 6本
    float32_t3(0.0000000f,  0.5257311f,  0.8506508f),
    float32_t3(0.0000000f, -0.5257311f,  0.8506508f),
    float32_t3(0.5257311f,  0.8506508f,  0.0000000f),
    float32_t3(-0.5257311f, 0.8506508f,  0.0000000f),
    float32_t3(0.8506508f,  0.0000000f,  0.5257311f),
    float32_t3(0.8506508f,  0.0000000f, -0.5257311f),
    // 正十二面体の頂点(六角形セルの中心) 10本
    float32_t3(0.5773503f,  0.5773503f,  0.5773503f),
    float32_t3(0.5773503f,  0.5773503f, -0.5773503f),
    float32_t3(0.5773503f, -0.5773503f,  0.5773503f),
    float32_t3(0.5773503f, -0.5773503f, -0.5773503f),
    float32_t3(0.0000000f,  0.3568221f,  0.9341724f),
    float32_t3(0.0000000f, -0.3568221f,  0.9341724f),
    float32_t3(0.3568221f,  0.9341724f,  0.0000000f),
    float32_t3(-0.3568221f, 0.9341724f,  0.0000000f),
    float32_t3(0.9341724f,  0.0000000f,  0.3568221f),
    float32_t3(0.9341724f,  0.0000000f, -0.3568221f),
};

// セルの境界線の太さ(最近傍と次近傍の内積差のしきい値)。大きいほど枠が太い。
static const float32_t kBarrierLineWidth = 0.045f;
// セル内側の不透明度(Base Colorのαに対する割合)。
static const float32_t kBarrierFillAlpha = 0.18f;
// 縁(視線と垂直な部分)の光り方。ドーム状に見せるためのフレネル。
static const float32_t kBarrierRimPower = 2.5f;

// 衝撃波の輪の断面。外側(進行方向)を鋭く、内側を長く尾引かせると「通り過ぎた圧」に見える。
static const float32_t kShockwaveFrontWidth = 0.35f;
static const float32_t kShockwaveTailWidth = 0.75f;

// トレイルの幅方向のぼかし量。大きいほど縁が柔らかい。
static const float32_t kTrailEdgeSoftness = 0.6f;

PixelShaderOutput main(VertexShaderOutput input)
{
    // フラットシェーディング: 以降のすべての方式が、頂点の法線の代わりに面の法線を使う。
    if (gMaterial.flatShading != 0)
    {
        input.normal = FlatNormal(input.worldPosition);
    }
    float32_t4 transformedUV = mul(float32_t4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    // triplanarScaleが0のときは今までどおりメッシュのUVで貼る(スクロールや板ポリはこちら)。
    float32_t4 textureColor = (gMaterial.triplanarScale > 0.0f)
        ? SampleTriplanar(input.worldPosition, input.normal, gMaterial.triplanarScale)
        : SampleTexture(gTexture, transformedUV.xy);
    PixelShaderOutput output;
    // エミッションRTは既定で書き込みなし(黒)。Emissionチェック付きマテリアルだけが下で上書きする。
    output.emission = float32_t4(0.0f, 0.0f, 0.0f, 1.0f);
    // ライティング各処理
    if (gMaterial.enableLighting != 0)
    { // Lightingする場合
    
        if (gMaterial.enableLighting == 1)
        { // lambertModel (DirectionalLight + PointLight + SpotLight の拡散反射)
            float32_t3 normal = normalize(input.normal);

            // DirectionalLight
            float cos = saturate(dot(normal, -normalize(gDirectionalLight.direction)));
            float32_t3 result = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * cos * gDirectionalLight.intensity;

            // PointLights (拡散反射)
            for (int32_t i = 0; i < pointLightCount; i++)
            {
                float32_t3 pointLightDirection = normalize(pointLights[i].position - input.worldPosition);
                float32_t pointLightCos = saturate(dot(normal, pointLightDirection));
                float32_t distance = length(pointLights[i].position - input.worldPosition);
                float32_t factor = pow(saturate(-distance / pointLights[i].radius + 1.0f), pointLights[i].decay);
                result += gMaterial.color.rgb * textureColor.rgb * pointLights[i].color.rgb * pointLightCos * pointLights[i].intensity * factor;
            }

            // SpotLights (拡散反射)
            for (int32_t s = 0; s < spotLightCount; s++)
            {
                float32_t3 spotLightDirection = normalize(spotLights[s].position - input.worldPosition);
                float32_t spotLightCos = saturate(dot(normal, spotLightDirection));
                float32_t3 spotLightDirectionOnSurface = normalize(input.worldPosition - spotLights[s].position);
                float32_t cosAngle = dot(spotLightDirectionOnSurface, normalize(spotLights[s].direction));
                float32_t falloffFactor = saturate((cosAngle - spotLights[s].cosAngle) / (spotLights[s].cosFalloffStart - spotLights[s].cosAngle));
                float32_t distance = length(spotLights[s].position - input.worldPosition);
                float32_t attenuationFactor = pow(saturate(-distance / spotLights[s].distance + 1.0f), spotLights[s].decay);
                result += gMaterial.color.rgb * textureColor.rgb * spotLights[s].color.rgb * spotLightCos * spotLights[s].intensity * attenuationFactor * falloffFactor;
            }

            output.color.rgb = result;
            output.color.a = gMaterial.color.a * textureColor.a;
        }
        else if (gMaterial.enableLighting == 2)
        { // halfLambertModel (DirectionalLight + PointLight + SpotLight の拡散反射)
            float32_t3 normal = normalize(input.normal);

            // DirectionalLight
            float NdotL = dot(normal, -normalize(gDirectionalLight.direction));
            float cos = pow(NdotL * 0.5f + 0.5f, 2.0f);
            float32_t3 result = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * cos * gDirectionalLight.intensity;

            // PointLights (ハーフランバート拡散)
            for (int32_t i = 0; i < pointLightCount; i++)
            {
                float32_t3 pointLightDirection = normalize(pointLights[i].position - input.worldPosition);
                float32_t pointNdotL = dot(normal, pointLightDirection);
                float32_t pointLightCos = pow(pointNdotL * 0.5f + 0.5f, 2.0f);
                float32_t distance = length(pointLights[i].position - input.worldPosition);
                float32_t factor = pow(saturate(-distance / pointLights[i].radius + 1.0f), pointLights[i].decay);
                result += gMaterial.color.rgb * textureColor.rgb * pointLights[i].color.rgb * pointLightCos * pointLights[i].intensity * factor;
            }

            // SpotLights (ハーフランバート拡散)
            for (int32_t s = 0; s < spotLightCount; s++)
            {
                float32_t3 spotLightDirection = normalize(spotLights[s].position - input.worldPosition);
                float32_t spotNdotL = dot(normal, spotLightDirection);
                float32_t spotLightCos = pow(spotNdotL * 0.5f + 0.5f, 2.0f);
                float32_t3 spotLightDirectionOnSurface = normalize(input.worldPosition - spotLights[s].position);
                float32_t cosAngle = dot(spotLightDirectionOnSurface, normalize(spotLights[s].direction));
                float32_t falloffFactor = saturate((cosAngle - spotLights[s].cosAngle) / (spotLights[s].cosFalloffStart - spotLights[s].cosAngle));
                float32_t distance = length(spotLights[s].position - input.worldPosition);
                float32_t attenuationFactor = pow(saturate(-distance / spotLights[s].distance + 1.0f), spotLights[s].decay);
                result += gMaterial.color.rgb * textureColor.rgb * spotLights[s].color.rgb * spotLightCos * spotLights[s].intensity * attenuationFactor * falloffFactor;
            }

            output.color.rgb = result;
            output.color.a = gMaterial.color.a * textureColor.a;
        }
        else if (gMaterial.enableLighting == 3)
        { // PhongReflectionModel (ランバート拡散 + Phong鏡面 + PointLight + SpotLight)
            float32_t3 normal = normalize(input.normal);
            // カメラへの方向を算出する。数式のv
            float32_t3 toEye = normalize(gCamera.worldPosition - input.worldPosition);

            // --- DirectionalLight ---
            // 拡散はランバート(saturate)にする。ハーフランバートだと裏面も明るく影が出ないため。
            float cos = saturate(dot(normal, -normalize(gDirectionalLight.direction)));
            float32_t3 reflectLight = reflect(normalize(gDirectionalLight.direction), normal);
            float32_t specularPow = pow(saturate(dot(reflectLight, toEye)), gMaterial.shininess);
            float32_t3 diffuse = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * cos * gDirectionalLight.intensity;
            float32_t3 specular = gDirectionalLight.color.rgb * gDirectionalLight.intensity * specularPow * float32_t3(1.0f, 1.0f, 1.0f);
            float32_t3 result = diffuse + specular;

            // --- PointLights ---
            for (int32_t i = 0; i < pointLightCount; i++)
            {
                float32_t3 pointLightDirection = normalize(pointLights[i].position - input.worldPosition);
                float32_t pointLightCos = saturate(dot(normal, pointLightDirection));
                float32_t3 pointReflect = reflect(-pointLightDirection, normal);
                float32_t pointSpecularPow = pow(saturate(dot(pointReflect, toEye)), gMaterial.shininess);
                float32_t distance = length(pointLights[i].position - input.worldPosition);
                float32_t factor = pow(saturate(-distance / pointLights[i].radius + 1.0f), pointLights[i].decay);
                result += gMaterial.color.rgb * textureColor.rgb * pointLights[i].color.rgb * pointLightCos * pointLights[i].intensity * factor;
                result += pointLights[i].color.rgb * pointLights[i].intensity * pointSpecularPow * float32_t3(1.0f, 1.0f, 1.0f) * factor;
            }

            // --- SpotLights ---
            for (int32_t s = 0; s < spotLightCount; s++)
            {
                float32_t3 spotLightDirection = normalize(spotLights[s].position - input.worldPosition);
                float32_t spotLightCos = saturate(dot(normal, spotLightDirection));
                float32_t3 spotReflect = reflect(-spotLightDirection, normal);
                float32_t spotSpecularPow = pow(saturate(dot(spotReflect, toEye)), gMaterial.shininess);
                float32_t3 spotLightDirectionOnSurface = normalize(input.worldPosition - spotLights[s].position);
                float32_t cosAngle = dot(spotLightDirectionOnSurface, normalize(spotLights[s].direction));
                float32_t falloffFactor = saturate((cosAngle - spotLights[s].cosAngle) / (spotLights[s].cosFalloffStart - spotLights[s].cosAngle));
                float32_t distance = length(spotLights[s].position - input.worldPosition);
                float32_t attenuationFactor = pow(saturate(-distance / spotLights[s].distance + 1.0f), spotLights[s].decay);
                result += gMaterial.color.rgb * textureColor.rgb * spotLights[s].color.rgb * spotLightCos * spotLights[s].intensity * attenuationFactor * falloffFactor;
                result += spotLights[s].color.rgb * spotLights[s].intensity * spotSpecularPow * float32_t3(1.0f, 1.0f, 1.0f) * attenuationFactor * falloffFactor;
            }

            output.color.rgb = result;
            output.color.a = gMaterial.color.a * textureColor.a;
        }
        else if (gMaterial.enableLighting == 4)
        { // BlingPhongReflectionModel
            
            float NdotL = dot(normalize(input.normal), -normalize(gDirectionalLight.direction));
            float cos = pow(NdotL * 0.5f + 0.5f, 2.0f);
            // カメラへの方向を算出する。数式のv
            float32_t3 toEye = normalize(gCamera.worldPosition - input.worldPosition);
    
            // DirectionalLight計算処理
            // ----------------------------------
            
            // 入射角の反射ベクトルを求める。数式のr
            float32_t3 reflectLight = reflect(normalize(gDirectionalLight.direction), normalize(input.normal));

            // 内積をとって、saturateして、shininessを階乗すると鏡面反射の強度が求まる
            float32_t3 halfVector = normalize(-gDirectionalLight.direction + toEye);
            float NDotH = dot(normalize(input.normal), halfVector);
            float specularPow = pow(saturate(NDotH), gMaterial.shininess);
            
            // 拡散反射
            float32_t3 directionalLightDiffuse = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * cos * gDirectionalLight.intensity;
            // 鏡面反射
            float32_t3 directionalLightSpecular = gDirectionalLight.color.rgb * gDirectionalLight.intensity * specularPow * float32_t3(1.0f, 1.0f, 1.0f);
            // 拡散反射+鏡面反射
            output.color.rgb = directionalLightDiffuse + directionalLightSpecular;
            
            // PointLights実装
            // ----------------------------------
            for (int32_t i = 0; i < pointLightCount; i++)
            {
                // 方向
                float32_t3 pointLightDirection = normalize(pointLights[i].position - input.worldPosition);
                float32_t pointLightCos = saturate(dot(normalize(input.normal), pointLightDirection));
                float32_t3 halfVectorPoint = normalize(pointLightDirection + toEye);
                float32_t pointLightSpecularPow = pow(saturate(dot(normalize(input.normal), halfVectorPoint)), gMaterial.shininess);
                
 
                float32_t distance = length(pointLights[i].position - input.worldPosition); // ポイントライトへの距離
                float32_t factor = pow(saturate(-distance / pointLights[i].radius + 1.0f), pointLights[i].decay); // 指数によるコントロール
                
                // 拡散反射
                float32_t3 pointLightDiffuse = gMaterial.color.rgb * textureColor.rgb * pointLights[i].color.rgb * pointLightCos * pointLights[i].intensity * factor;
                // 鏡面反射
                float32_t3 pointLightSpecular = pointLights[i].color.rgb * pointLights[i].intensity * pointLightSpecularPow * float32_t3(1.0f, 1.0f, 1.0f) * factor;
                
                output.color.rgb += pointLightDiffuse + pointLightSpecular;
 
            }
            
            // SpotLights実装
            // ----------------------------------
            for (int32_t s = 0; s < spotLightCount; s++)
            {
                // 方向
                float32_t3 spotLightDirection = normalize(spotLights[s].position - input.worldPosition);
                float32_t spotLightCos = saturate(dot(normalize(input.normal), spotLightDirection));
                float32_t3 halfVectorSpot = normalize(spotLightDirection + toEye);
                float32_t spotLightSpecularPow = pow(saturate(dot(normalize(input.normal), halfVectorSpot)), gMaterial.shininess);

                float32_t3 spotLightDirectionOnSurface = normalize(input.worldPosition - spotLights[s].position);

                float32_t cosAngle = dot(spotLightDirectionOnSurface, normalize(spotLights[s].direction));
                float32_t falloffFactor = saturate((cosAngle - spotLights[s].cosAngle) / (spotLights[s].cosFalloffStart - spotLights[s].cosAngle));

                float32_t distance = length(spotLights[s].position - input.worldPosition);
                float32_t attenuationFactor = pow(saturate(-distance / spotLights[s].distance + 1.0f), spotLights[s].decay);

                // 拡散反射
                float32_t3 spotLightDiffuse = gMaterial.color.rgb * textureColor.rgb * spotLights[s].color.rgb * spotLightCos * spotLights[s].intensity * attenuationFactor * falloffFactor;
                // 鏡面反射
                float32_t3 spotLightSpecular = spotLights[s].color.rgb * spotLights[s].intensity * spotLightSpecularPow * float32_t3(1.0f, 1.0f, 1.0f) * attenuationFactor * falloffFactor;

                output.color.rgb += spotLightDiffuse + spotLightSpecular;

            }
            // アルファは今まで通り
            output.color.a = gMaterial.color.a * textureColor.a;
        }
        else if (gMaterial.enableLighting == 5)
        { // 衝撃波の輪(Ringプリミティブ前提)。土煙なので発光させず、帯の内外へ向けて薄くするだけ。
            // v は帯の内周(0)→外周(1)。両端を0にして、輪の境界が線で切れないようにする。
            float32_t band = input.texcoord.y;
            // 外側は鋭く立ち上がり、内側は長く尾を引く(圧が前面に集まり、後ろへ散っていく見え方)。
            float32_t front = smoothstep(0.0f, kShockwaveFrontWidth, 1.0f - band);
            float32_t tail = smoothstep(0.0f, kShockwaveTailWidth, band);
            float32_t profile = saturate(front * tail);

            output.color.rgb = gMaterial.color.rgb * textureColor.rgb;
            output.color.a = gMaterial.color.a * textureColor.a * profile;
            // 完全に透けた部分はPS末尾でdiscardされるので、半透明が深度バッファを汚さない。
        }
        else if (gMaterial.enableLighting == 7)
        { // トレイル(TrailRendererComponentの帯)。u=先頭(0)→末尾(1) / v=幅方向(0〜1)。
            // 末尾ほど薄くする。帯の終端が線でスパッと切れないようにするのが主目的。
            float32_t tailFade = 1.0f - input.texcoord.x;
            // 幅方向の縁もぼかして、板の輪郭が出ないようにする。
            float32_t edge = 1.0f - abs(input.texcoord.y * 2.0f - 1.0f);
            float32_t profile = saturate(tailFade * smoothstep(0.0f, kTrailEdgeSoftness, edge));

            output.color.rgb = gMaterial.color.rgb * textureColor.rgb;
            output.color.a = gMaterial.color.a * textureColor.a * profile;
        }
        else if (gMaterial.enableLighting == 8)
        { // トゥーン(セル調)。明るさを段に分け、暗い側は黒ではなく影の色(世界共通。Directional Light の Shadow Color)で塗る。
            float32_t3 normal = normalize(input.normal);
            float32_t3 albedo = gMaterial.color.rgb * textureColor.rgb;

            // DirectionalLight: いちばん暗い段 = 元の色×影の色、いちばん明るい段 = 元の色×ライトの色。その間を段で分ける。
            // ライトが影の色より暗い(強さ0など)ときに、光の当たる側が影より暗くならないよう、明るい段は影の色を下限にする。
            float32_t lit = ToonStep(saturate(dot(normal, -normalize(gDirectionalLight.direction))));
            float32_t3 shadow = albedo * gDirectionalLight.shadowColor;
            float32_t3 bright = max(albedo * gDirectionalLight.color.rgb * gDirectionalLight.intensity, shadow);
            float32_t3 result = lerp(shadow, bright, lit);

            // PointLights / SpotLights: 当たる量(角度×距離の減衰)を段に丸めて足す。
            for (int32_t i = 0; i < pointLightCount; i++)
            {
                float32_t3 pointLightDirection = normalize(pointLights[i].position - input.worldPosition);
                float32_t pointLightCos = saturate(dot(normal, pointLightDirection));
                float32_t distance = length(pointLights[i].position - input.worldPosition);
                float32_t factor = pow(saturate(-distance / pointLights[i].radius + 1.0f), pointLights[i].decay);
                result += albedo * pointLights[i].color.rgb * pointLights[i].intensity * ToonStep(pointLightCos * factor);
            }
            for (int32_t s = 0; s < spotLightCount; s++)
            {
                float32_t3 spotLightDirection = normalize(spotLights[s].position - input.worldPosition);
                float32_t spotLightCos = saturate(dot(normal, spotLightDirection));
                float32_t3 spotLightDirectionOnSurface = normalize(input.worldPosition - spotLights[s].position);
                float32_t cosAngle = dot(spotLightDirectionOnSurface, normalize(spotLights[s].direction));
                float32_t falloffFactor = saturate((cosAngle - spotLights[s].cosAngle) / (spotLights[s].cosFalloffStart - spotLights[s].cosAngle));
                float32_t distance = length(spotLights[s].position - input.worldPosition);
                float32_t attenuationFactor = pow(saturate(-distance / spotLights[s].distance + 1.0f), spotLights[s].decay);
                result += albedo * spotLights[s].color.rgb * spotLights[s].intensity * ToonStep(spotLightCos * attenuationFactor * falloffFactor);
            }

            output.color.rgb = result;
            output.color.a = gMaterial.color.a * textureColor.a;
        }
        else if (gMaterial.enableLighting == 6)
        { // バリア: 六角形20枚+五角形12枚のセルが浮かぶ半透明シェル
            // 球の法線はそのまま中心からの向きなので、UVを使わずにセルを求められる
            // (球のUVは極で潰れるため、UVで貼ると模様が歪む)。
            float32_t3 normal = normalize(input.normal);

            // 最も近いセル中心(d1)と2番目(d2)。対になる向きは abs でまとめている。
            float32_t d1 = -2.0f;
            float32_t d2 = -2.0f;
            [unroll]
            for (int32_t c = 0; c < 16; c++)
            {
                float32_t d = abs(dot(normal, kBarrierCells[c]));
                if (d > d1)
                {
                    d2 = d1;
                    d1 = d;
                }
                else if (d > d2)
                {
                    d2 = d;
                }
            }

            // d1とd2が拮抗している場所 = 2つのセルの境界。ここを線として光らせる。
            float32_t edge = 1.0f - smoothstep(0.0f, kBarrierLineWidth, d1 - d2);

            // 縁(視線に対して垂直に近い面)を光らせてドーム感を出す。
            float32_t3 toEye = normalize(gCamera.worldPosition - input.worldPosition);
            float32_t rim = pow(1.0f - saturate(abs(dot(normal, toEye))), kBarrierRimPower);

            float32_t glow = saturate(edge + rim * 0.6f);

            output.color.rgb = gMaterial.color.rgb * textureColor.rgb * (0.6f + glow * 2.0f);
            // 面の内側は薄く、枠と縁だけがはっきり出る。
            output.color.a = gMaterial.color.a * textureColor.a * saturate(kBarrierFillAlpha + glow);

            // 光る部分だけをブルームへ回す(一様に発光させると模様が潰れるため、
            // マテリアルのEmissionチェックには頼らずここで書く)。
            if (gMaterial.emissiveEnabled == 0)
            {
                float32_t3 barrierEmissive = gMaterial.color.rgb * glow * gMaterial.bloomIntensity;
                output.emission.rgb = min(barrierEmissive, 65000.0f);
            }
        }
        else
        {
            float cos = saturate(dot(normalize(input.normal), -gDirectionalLight.direction)); // lambertModel

            output.color.rgb = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * cos * gDirectionalLight.intensity;
            output.color.a = gMaterial.color.a * textureColor.a;
        }
    }
    else
    { //Lightingしない場合。前回までと同じ演算
        output.color = gMaterial.color * textureColor;
    }

    // エミッション(自己発光)。Emissionチェックが付いたマテリアルだけ、
    // ライティングの有無に関わらず加算する(αには影響させない)。
    if (gMaterial.emissiveEnabled != 0)
    {
        // エミッションマップ(t2)を乗算する。**マップ未指定のマテリアルには白1x1が入る**ので、
        // ここに分岐は要らない(白=1倍=マップ無しと同じ)。黒い箇所は光らず、白い箇所だけが光る。
        float32_t3 emissiveMask = SampleTexture(gEmissiveTexture, transformedUV.xy).rgb;
        float32_t3 emissive = gMaterial.emissiveColor * gMaterial.emissiveIntensity * emissiveMask;
        output.color.rgb += emissive;

        // 露出光(ブルーム)へ回す成分。マテリアル別の閾値(soft knee付き)で絞り、滲み強度を掛けて
        // エミッション専用RTへ書く。ブルームチェーンはこのRTだけを入力にする。
        float32_t brightness = max(emissive.r, max(emissive.g, emissive.b));
        float32_t knee = gMaterial.bloomThreshold * gMaterial.bloomSoftKnee;
        float32_t soft = clamp(brightness - gMaterial.bloomThreshold + knee, 0.0f, 2.0f * knee);
        soft = soft * soft / (4.0f * knee + 1e-4f);
        float32_t contribution = max(soft, brightness - gMaterial.bloomThreshold) / max(brightness, 1e-4f);
        // FLOAT16/R11G11B10の飽和対策クランプ。
        output.emission.rgb = min(emissive * contribution * gMaterial.bloomIntensity, 65000.0f);
    }

    if (textureColor.a <= 0.5)
    {
        discard;
    }
    if (textureColor.a == 0.0)
    {
        discard;
    }
    if (output.color.a == 0.0)
    {
        discard;
    }
    
    return output;
}
