#pragma once

#include "../runtime/KujataApi.h"
#include <cstdint>
#include <string>

namespace KujataEngine {

/// <summary>
/// FractalNoise(math/Noise.h)からRGBA8テクスチャを起動時にメモリ生成する。
/// ファイルには保存しない(TextureManager::CreateTextureFromMemoryで都度アップロード)。
/// 同じkeyへは1回しか実際の生成をしない(内部でSRVインデックスをキャッシュする)。
/// </summary>
namespace NoiseTexture {

/// <summary>
/// 岩肌用のアルベドテクスチャを生成し、SRVインデックスを返す。
/// 低周波の"染み"と高周波の"ざらつき"を重ねたfBmを、グレーがかった岩色へマッピングする。
/// </summary>
/// <param name="size">一辺のピクセル数(正方形)。</param>
/// <param name="seedLane">Noiseの第2引数に使うレーン。値を変えると別の模様になる。</param>
KUJATA_API uint32_t GenerateRockAlbedoTexture(uint32_t size = 256, float seedLane = 0.0f);

/// <summary>
/// **ひび割れた岩。** 岩肌の上に、細胞状に区切った境目を暗い線として焼き込む。
/// 線の太さが場所ごとに変わるので、割れ目が均等に走る格子には見えない。
/// </summary>
KUJATA_API uint32_t GenerateCrackedRockTexture(uint32_t size = 256, float seedLane = 880.0f);

/// <summary>
/// 土(地面)用のアルベドテクスチャを生成し、SRVインデックスを返す。
/// 岩肌より暗く・彩度低めの茶色で、低周波の"塊"を主体にした柔らかい表情にする。
/// </summary>
/// <param name="size">一辺のピクセル数(正方形)。</param>
/// <param name="seedLane">Noiseの第2引数に使うレーン。値を変えると別の模様になる。</param>
KUJATA_API uint32_t GenerateSoilAlbedoTexture(uint32_t size = 256, float seedLane = 50.0f);

/// <summary>
/// 苔むした岩のアルベドテクスチャを生成し、SRVインデックスを返す。
/// 岩肌の**窪んだ所にだけ苔を溜める**(高い所は剥き出しの岩)。
/// 一様に緑を混ぜると「緑に塗った石」になるので、分布そのもので苔らしさを出している。
/// </summary>
/// <param name="size">一辺のピクセル数(正方形)。</param>
/// <param name="seedLane">Noiseの第2引数に使うレーン。値を変えると別の模様になる。</param>
KUJATA_API uint32_t GenerateMossyRockTexture(uint32_t size = 256, float seedLane = 700.0f);

/// <summary>
/// 砂地のアルベドテクスチャを生成し、SRVインデックスを返す。
/// 大きなうねり(砂丘)+ 風紋の縞 + 細かい砂粒の3層。縞はノイズで曲げて人工物っぽさを消している。
/// </summary>
/// <param name="size">一辺のピクセル数(正方形)。</param>
/// <param name="seedLane">Noiseの第2引数に使うレーン。値を変えると別の模様になる。</param>
KUJATA_API uint32_t GenerateSandTexture(uint32_t size = 256, float seedLane = 760.0f);

/// <summary>
/// 錆びた金属のアルベドテクスチャを生成し、SRVインデックスを返す。
/// 鉄の地に、錆の「塊」と**そこから下へ流れた筋**を重ねる。機械や門・武器向け。
/// </summary>
/// <param name="size">一辺のピクセル数(正方形)。</param>
/// <param name="seedLane">Noiseの第2引数に使うレーン。値を変えると別の模様になる。</param>
KUJATA_API uint32_t GenerateRustedMetalTexture(uint32_t size = 256, float seedLane = 820.0f);

/// <summary>
/// 土埃パーティクル用のスプライトテクスチャを生成し、SRVインデックスを返す。
/// 円形のソフトなアルファ減衰にfBmで揺らぎを加え、もやついた埃の塊に見せる。
/// </summary>
/// <param name="size">一辺のピクセル数(正方形)。</param>
/// <param name="seedLane">Noiseの第2引数に使うレーン。値を変えると別の模様になる。</param>
KUJATA_API uint32_t GenerateDustSpriteTexture(uint32_t size = 128, float seedLane = 100.0f);

/// <summary>
/// タイトル背景などに流す「靄」用テクスチャを生成し、SRVインデックスを返す。
/// RGBは白のままにしてαだけにfBmを載せるので、**濃さも色もImageのColorで決められる**。
/// **上下左右で継ぎ目なく繰り返す**(MakeTileable)ため、UVをスクロールさせても切れ目が出ない。
/// </summary>
/// <param name="size">一辺のピクセル数(正方形)。</param>
/// <param name="seedLane">Noiseの第2引数に使うレーン。値を変えると別の模様になる。</param>
/// <param name="frequency">模様の細かさ。小さいほど大きなうねり、大きいほど細かい筋になる。</param>
/// <param name="contrast">αの立ち上がりの鋭さ。大きいほど濃淡がはっきりする。</param>
KUJATA_API uint32_t GenerateMistTexture(uint32_t size = 256, float seedLane = 200.0f, float frequency = 3.0f, float contrast = 1.6f);

/// <summary>
/// **3Dモデル(板ポリ)に貼る**霧のテクスチャを生成し、SRVインデックスを返す。霧の扉に使う。
///
/// GenerateMistTextureとの違いは**濃さをαではなくRGBに入れる**こと。Object3dのPixelShaderは
/// `textureColor.a &lt;= 0.5` でdiscardする(アルファテスト)ので、α勾配のテクスチャをモデルへ貼ると
/// **霧ではなく穴だらけの型抜き**になってしまう。加算合成(Material の Blend Mode = 2)で
/// 「黒=見えない・白=濃い」として使えば、その分岐に触れずに滑らかな霧になる。
///
/// 横に細かく縦に伸びた**縦筋**の模様で、上下左右に継ぎ目なく繰り返す(UVを流しても切れ目が出ない)。
/// </summary>
/// <param name="size">一辺のピクセル数(正方形)。</param>
/// <param name="seedLane">ノイズを読み出す位置。層ごとに必ず別の値にする。</param>
/// <param name="frequency">模様の細かさ。小さいほど大きなうねり、大きいほど細かい筋になる。</param>
/// <param name="contrast">濃淡の締まり。大きいほど濃い所と薄い所の差が出る。</param>
KUJATA_API uint32_t GenerateFogSheetTexture(uint32_t size = 256, float seedLane = 900.0f, float frequency = 3.0f, float contrast = 1.6f);

/// <summary>
/// 炎のパーティクル用スプライトを生成し、SRVインデックスを返す。
/// **RGBは白のまま**にしてαだけで形を作るので、色はParticleSystemのStart/End Colorで決める。
/// 中心を強く残した放射状の減衰にfBmで舌先の揺らぎを足し、炎の粒に見せる。
/// </summary>
/// <param name="size">一辺のピクセル数(正方形)。</param>
/// <param name="seedLane">Noiseの第2引数に使うレーン。値を変えると別の形になる。</param>
KUJATA_API uint32_t GenerateFlameSpriteTexture(uint32_t size = 128, float seedLane = 300.0f);

} // namespace NoiseTexture

} // namespace KujataEngine
