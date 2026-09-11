#include "Noise.h"

#include <algorithm>
#include <cmath>

namespace KujataEngine {

namespace {

// Ken Perlinの参照実装と同じ並び。ハードコードなので実行ごと・環境ごとに結果が変わらない。
constexpr int kPermutation[256] = {
    151, 160, 137, 91,  90,  15,  131, 13,  201, 95,  96,  53,  194, 233, 7,   225, 140, 36,  103, 30,  69,  142, 8,   99,
    37,  240, 21,  10,  23,  190, 6,   148, 247, 120, 234, 75,  0,   26,  197, 62,  94,  252, 219, 203, 117, 35,  11,  32,
    57,  177, 33,  88,  237, 149, 56,  87,  174, 20,  125, 136, 171, 168, 68,  175, 74,  165, 71,  134, 139, 48,  27,  166,
    77,  146, 158, 231, 83,  111, 229, 122, 60,  211, 133, 230, 220, 105, 92,  41,  55,  46,  245, 40,  244, 102, 143, 54,
    65,  25,  63,  161, 1,   216, 80,  73,  209, 76,  132, 187, 208, 89,  18,  169, 200, 196, 135, 130, 116, 188, 159, 86,
    164, 100, 109, 198, 173, 186, 3,   64,  52,  217, 226, 250, 124, 123, 5,   202, 38,  147, 118, 126, 255, 82,  85,  212,
    207, 206, 59,  227, 47,  16,  58,  17,  182, 189, 28,  42,  223, 183, 170, 213, 119, 248, 152, 2,   44,  154, 163, 70,
    221, 153, 101, 155, 167, 43,  172, 9,   129, 22,  39,  253, 19,  98,  108, 110, 79,  113, 224, 232, 178, 185, 112, 104,
    218, 246, 97,  228, 251, 34,  242, 193, 238, 210, 144, 12,  191, 179, 162, 241, 81,  51,  145, 235, 249, 14,  239, 107,
    49,  192, 214, 31,  181, 199, 106, 157, 184, 84,  204, 176, 115, 121, 50,  45,  127, 4,   150, 254, 138, 236, 205, 93,
    222, 114, 67,  29,  24,  72,  243, 141, 128, 195, 78,  66,  215, 61,  156, 180,
};

/// <summary>表引き。256で折り返すので、いくつを渡しても安全。</summary>
inline int Hash(int index) { return kPermutation[index & 255]; }

/// <summary>
/// 補間カーブ 6t^5-15t^4+10t^3。両端で1次・2次微分が0になるため、
/// 格子をまたぐ場所に継ぎ目(マッハバンド)が出ない。改良版パーリンの肝。
/// </summary>
inline float Fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

inline float Mix(float a, float b, float t) { return a + (b - a) * t; }

/// <summary>格子点の勾配ベクトルと相対位置の内積。12方向から選ぶので方向の偏りが小さい。</summary>
inline float Grad(int hash, float x, float y, float z) {
	int h = hash & 15;
	float u = (h < 8) ? x : y;
	float v = (h < 4) ? y : ((h == 12 || h == 14) ? x : z);
	return (((h & 1) == 0) ? u : -u) + (((h & 2) == 0) ? v : -v);
}

float Noise3(float x, float y, float z) {
	float floorX = std::floor(x);
	float floorY = std::floor(y);
	float floorZ = std::floor(z);

	// 格子のどのマスにいるか。
	int cellX = static_cast<int>(floorX) & 255;
	int cellY = static_cast<int>(floorY) & 255;
	int cellZ = static_cast<int>(floorZ) & 255;

	// マス内のどこにいるか(0〜1)。
	float localX = x - floorX;
	float localY = y - floorY;
	float localZ = z - floorZ;

	float u = Fade(localX);
	float v = Fade(localY);
	float w = Fade(localZ);

	// 8つの角の勾配を引くためのハッシュ。
	int a = Hash(cellX) + cellY;
	int aa = Hash(a) + cellZ;
	int ab = Hash(a + 1) + cellZ;
	int b = Hash(cellX + 1) + cellY;
	int ba = Hash(b) + cellZ;
	int bb = Hash(b + 1) + cellZ;

	// 8隅の内積をx→y→zの順に補間する。
	return Mix(
	    Mix(Mix(Grad(Hash(aa), localX, localY, localZ), Grad(Hash(ba), localX - 1.0f, localY, localZ), u),
	        Mix(Grad(Hash(ab), localX, localY - 1.0f, localZ), Grad(Hash(bb), localX - 1.0f, localY - 1.0f, localZ), u), v),
	    Mix(Mix(Grad(Hash(aa + 1), localX, localY, localZ - 1.0f), Grad(Hash(ba + 1), localX - 1.0f, localY, localZ - 1.0f), u),
	        Mix(Grad(Hash(ab + 1), localX, localY - 1.0f, localZ - 1.0f),
	            Grad(Hash(bb + 1), localX - 1.0f, localY - 1.0f, localZ - 1.0f), u),
	        v),
	    w);
}

float Fractal(float x, float y, float z, int octaves, float lacunarity, float gain) {
	int count = std::clamp(octaves, 1, 12);
	float amplitude = 1.0f;
	float frequency = 1.0f;
	float sum = 0.0f;
	float totalAmplitude = 0.0f;

	for (int index = 0; index < count; ++index) {
		sum += Noise3(x * frequency, y * frequency, z * frequency) * amplitude;
		totalAmplitude += amplitude;
		frequency *= lacunarity;
		amplitude *= gain;
	}

	// 振幅の合計で割って、オクターブ数を変えても戻り値の幅が変わらないようにする。
	return (totalAmplitude > 0.0f) ? (sum / totalAmplitude) : 0.0f;
}

} // namespace

// zに半端な値を入れて格子面を外す。z=0(整数)のスライスでは勾配の半分が消えてしまい、
// 振幅が痩せる(実測で±0.50止まり)うえ、yを変えただけのレーン同士が似た波形になる。
constexpr float kSliceZ = 0.31f;

float PerlinNoise(float x, float y) { return Noise3(x, y, kSliceZ); }


float PerlinNoise(const Vector3& position) { return Noise3(position.x, position.y, position.z); }

float FractalNoise(float x, float y, int octaves, float lacunarity, float gain) {
	return Fractal(x, y, kSliceZ, octaves, lacunarity, gain);
}

float FractalNoise(const Vector3& position, int octaves, float lacunarity, float gain) {
	return Fractal(position.x, position.y, position.z, octaves, lacunarity, gain);
}

} // namespace KujataEngine
