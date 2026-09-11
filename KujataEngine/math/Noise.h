#pragma once
#include "../runtime/KujataApi.h"
#include <math/Vector3.h>

namespace KujataEngine {

/// <summary>
/// パーリンノイズ(Ken Perlinの改良版)。座標を入れると「滑らかに繋がった乱数」が返る。
///
/// ただの乱数と違うのは次の3点で、使い道はここから決まる。
///   - **連続している**  隣の座標と滑らかに繋がるのでチラつかない。時間を入れれば揺れ、空間を入れれば模様になる
///   - **保存が要らない** 同じ座標には必ず同じ値が返るので、配列もテクスチャも持たなくてよい
///   - **重ねられる**    周波数を変えて足すと(FractalNoise)大きなうねりと細かいざらつきを同時に作れる
///
/// **第2引数はレーン(系統)として使うのが定石。** 例えばカメラの縦揺れと横揺れ、脚1〜4の揺れなど、
/// 「同じ時間を入れるが別々に動いてほしいもの」は y を離してやれば互いに無関係な揺れになる。
///
/// 戻り値の実測範囲は概ね **±0.8**(理論上限は±0.87)。2D版も内部は3Dノイズの薄切りなので、
/// 2Dの理論値±0.71ではなくこちらに従う。振幅を掛ける側はこの前提で係数を決めること。
/// **格子点(整数座標)では必ず0になる**ため、引数には半端な値を足しておくとよい。
/// </summary>
KUJATA_API float PerlinNoise(float x, float y);
KUJATA_API float PerlinNoise(const Vector3& position);

/// <summary>
/// 周波数を lacunarity 倍にしながら振幅を gain 倍にして octaves 回重ねたノイズ(fBm)。
/// 雲・地形・岩肌などディテールの階層が欲しいものに使う。
/// octavesを増やすほど細かくなるが比例して重くなるので、3〜4で足りることが多い。
/// 振幅の合計で割ってあるため、戻り値の範囲は PerlinNoise と同じままになる。
/// </summary>
KUJATA_API float FractalNoise(float x, float y, int octaves, float lacunarity = 2.0f, float gain = 0.5f);
KUJATA_API float FractalNoise(const Vector3& position, int octaves, float lacunarity = 2.0f, float gain = 0.5f);

} // namespace KujataEngine
