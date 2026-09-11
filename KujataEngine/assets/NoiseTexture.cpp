#include "NoiseTexture.h"
#include "../base/TextureManager.h"
#include "../math/Noise.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace KujataEngine {

namespace {

struct Color3 {
	float r = 0.0f;
	float g = 0.0f;
	float b = 0.0f;
};

/// <summary>
/// key(サイズ+シード)ごとにSRVインデックスを覚えておく。TextureManager::CreateTextureFromMemory
/// 自体もkeyでGPU生成をスキップするが、それだけだと**CPU側のノイズ計算(4隅ブレンドで4倍重い)は
/// 呼ばれるたびに毎回やり直してしまう**。土埃のように同じ設定のテクスチャを何度も要求される
/// 用途(プールが新しい器を作るたび)でカクつきの原因になっていたため、ここで丸ごとスキップする。
/// </summary>
std::unordered_map<std::string, uint32_t>& GeneratedTextureCache() {
	static std::unordered_map<std::string, uint32_t> cache;
	return cache;
}

Color3 Lerp(const Color3& a, const Color3& b, float t) {
	return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

uint8_t ToByte(float value) { return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f); }

/// <summary>
/// ノイズが0を跨ぐ場所(尾根)を薄い線として浮かび上がらせる"リッジノイズ"。
/// 1-|noise| は0跨ぎでピーク(≒1)になるので、それをべき乗で先鋭化すると
/// 枝分かれした細いひび割れ網に見える。Voronoi等の専用ノイズが無くても既存のFractalNoiseだけで作れる。
/// </summary>
float CrackMask(float u, float v, float freq, float seedLane, float sharpness) {
	float n = FractalNoise(u * freq + seedLane, v * freq + seedLane, 3);
	float ridge = 1.0f - std::fabs(n);
	return std::pow(std::clamp(ridge, 0.0f, 1.0f), sharpness);
}

/// <summary>
/// colorAt(u,v)を4隅(u,v / u-1,v / u,v-1 / u-1,v-1)で評価し、u,vの重みで混ぜて周期化する。
/// u=0とu=1(v=0とv=1も同様)がぴったり同じ値になるので、単純なWRAP(折り返し)だけで
/// タイルの継ぎ目が完全に連続する。MIRROR(反転)と違って対称の"合わせ目"が見た目に出ない。
/// </summary>
template <class ColorAtFunc>
Color3 MakeTileable(float u, float v, ColorAtFunc colorAt) {
	Color3 c00 = colorAt(u, v);
	Color3 c10 = colorAt(u - 1.0f, v);
	Color3 c01 = colorAt(u, v - 1.0f);
	Color3 c11 = colorAt(u - 1.0f, v - 1.0f);
	Color3 top = Lerp(c00, c10, u);
	Color3 bottom = Lerp(c01, c11, u);
	return Lerp(top, bottom, v);
}

} // namespace

namespace NoiseTexture {

uint32_t GenerateCrackedRockTexture(uint32_t size, float seedLane) {
	std::string key = "Generated/CrackedRock/" + std::to_string(size) + "/" + std::to_string(seedLane);
	if (auto cached = GeneratedTextureCache().find(key); cached != GeneratedTextureCache().end()) {
		return cached->second;
	}

	// 岩肌(低周波の染み + 高周波のざらつき)の上に、割れ目を線で焼く。
	// **割れ目はセル状の境目から作る。** 格子や筋ではなく、点を撒いて
	// 「一番近い点と二番目に近い点の差」を見ると、自然にひび割れの網になる。
	constexpr Color3 kDark = {0.10f, 0.09f, 0.08f};
	constexpr Color3 kLight = {0.56f, 0.53f, 0.48f};
	constexpr Color3 kCrack = {0.04f, 0.035f, 0.03f};

	// セルの種。タイルの継ぎ目で割れ目が切れないよう、周囲9タイルぶんを見る。
	constexpr int kCells = 6;
	auto cellPoint = [&](int cx, int cy) -> std::pair<float, float> {
		float fx = static_cast<float>(cx) * 12.9898f + static_cast<float>(cy) * 78.233f + seedLane;
		float fy = static_cast<float>(cx) * 39.3468f + static_cast<float>(cy) * 11.135f + seedLane * 1.7f;
		float rx = std::sin(fx) * 43758.5453f;
		float ry = std::sin(fy) * 24634.6345f;
		rx -= std::floor(rx);
		ry -= std::floor(ry);
		return {(static_cast<float>(cx) + rx) / static_cast<float>(kCells), (static_cast<float>(cy) + ry) / static_cast<float>(kCells)};
	};

	auto crackAt = [&](float u, float v) -> float {
		int gx = static_cast<int>(std::floor(u * kCells));
		int gy = static_cast<int>(std::floor(v * kCells));
		float best = 10.0f;
		float second = 10.0f;
		for (int oy = -1; oy <= 1; ++oy) {
			for (int ox = -1; ox <= 1; ++ox) {
				int cx = gx + ox;
				int cy = gy + oy;
				// タイル境界をまたいでも同じ種になるように巻き戻す(継ぎ目が出ない)
				int wx = ((cx % kCells) + kCells) % kCells;
				int wy = ((cy % kCells) + kCells) % kCells;
				auto p = cellPoint(wx, wy);
				float px = p.first + static_cast<float>(cx - wx) / static_cast<float>(kCells);
				float py = p.second + static_cast<float>(cy - wy) / static_cast<float>(kCells);
				float dx = px - u;
				float dy = py - v;
				float d = std::sqrt(dx * dx + dy * dy);
				if (d < best) { second = best; best = d; } else if (d < second) { second = d; }
			}
		}
		// 境目(1番目と2番目の差が小さい所)ほど濃い。差が開くほど岩肌のまま。
		float edge = second - best;
		float width = 0.020f + 0.016f * FractalNoise(u * 6.0f + seedLane, v * 6.0f + seedLane, 2);
		if (width < 0.006f) { width = 0.006f; }
		float c = 1.0f - std::clamp(edge / width, 0.0f, 1.0f);
		return c * c;
	};

	auto colorAt = [&](float su, float sv) -> Color3 {
		float base = FractalNoise(su * 4.0f + seedLane, sv * 4.0f + seedLane, 4);
		float grain = FractalNoise(su * 22.0f + seedLane + 7.3f, sv * 22.0f + seedLane + 7.3f, 2);
		float n = base * 0.72f + grain * 0.28f;
		float t = std::clamp(n * 0.65f + 0.5f, 0.0f, 1.0f);
		Color3 rock = Lerp(kDark, kLight, t);
		return Lerp(rock, kCrack, crackAt(su, sv));
	};

	std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);
	for (uint32_t y = 0; y < size; ++y) {
		float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
		for (uint32_t x = 0; x < size; ++x) {
			float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
			Color3 color = colorAt(u, v);
			size_t pixelIndex = (static_cast<size_t>(y) * size + x) * 4;
			pixels[pixelIndex + 0] = ToByte(color.r);
			pixels[pixelIndex + 1] = ToByte(color.g);
			pixels[pixelIndex + 2] = ToByte(color.b);
			pixels[pixelIndex + 3] = 255;
		}
	}

	uint32_t textureIndex = TextureManager::GetInstance()->CreateTextureFromMemory(key, pixels.data(), size, size);
	GeneratedTextureCache()[key] = textureIndex;
	return textureIndex;
}

uint32_t GenerateRockAlbedoTexture(uint32_t size, float seedLane) {
	std::string key = "Generated/RockAlbedo/" + std::to_string(size) + "/" + std::to_string(seedLane);
	if (auto cached = GeneratedTextureCache().find(key); cached != GeneratedTextureCache().end()) {
		return cached->second;
	}

	// 低周波の"染み"(base)と高周波の"ざらつき"(grain)を重ねる。
	// FractalNoiseの実測レンジは概ね±0.8なので、0..1へ寄せる係数はそれを踏まえた値にする。
	constexpr Color3 kDark = {0.12f, 0.10f, 0.09f};
	constexpr Color3 kLight = {0.58f, 0.55f, 0.50f};

	auto colorAt = [&](float su, float sv) -> Color3 {
		float base = FractalNoise(su * 4.0f + seedLane, sv * 4.0f + seedLane, 4);
		float grain = FractalNoise(su * 20.0f + seedLane + 7.3f, sv * 20.0f + seedLane + 7.3f, 2);
		float n = base * 0.75f + grain * 0.25f;
		float t = std::clamp(n * 0.65f + 0.5f, 0.0f, 1.0f);
		return Lerp(kDark, kLight, t);
	};

	std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);
	for (uint32_t y = 0; y < size; ++y) {
		float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
		for (uint32_t x = 0; x < size; ++x) {
			float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);

			Color3 color = MakeTileable(u, v, colorAt);
			size_t pixelIndex = (static_cast<size_t>(y) * size + x) * 4;
			pixels[pixelIndex + 0] = ToByte(color.r);
			pixels[pixelIndex + 1] = ToByte(color.g);
			pixels[pixelIndex + 2] = ToByte(color.b);
			pixels[pixelIndex + 3] = 255;
		}
	}

	uint32_t textureIndex = TextureManager::GetInstance()->CreateTextureFromMemory(key, pixels.data(), size, size);
	GeneratedTextureCache()[key] = textureIndex;
	return textureIndex;
}

uint32_t GenerateSoilAlbedoTexture(uint32_t size, float seedLane) {
	std::string key = "Generated/SoilAlbedo/" + std::to_string(size) + "/" + std::to_string(seedLane);
	if (auto cached = GeneratedTextureCache().find(key); cached != GeneratedTextureCache().end()) {
		return cached->second;
	}

	// 岩肌より低コントラスト・低彩度。低周波の"塊"を主体にし、高周波のざらつきは控えめにする
	// (乾いた土の柔らかい表情)。そこへ別レーンのCrackMaskを重ね、乾いてひび割れた地面にする。
	constexpr Color3 kDark = {0.10f, 0.07f, 0.04f};
	constexpr Color3 kLight = {0.42f, 0.30f, 0.18f};
	constexpr Color3 kCrackColor = {0.04f, 0.03f, 0.02f};

	auto colorAt = [&](float su, float sv) -> Color3 {
		float base = FractalNoise(su * 3.0f + seedLane, sv * 3.0f + seedLane, 4);
		float grain = FractalNoise(su * 14.0f + seedLane + 4.1f, sv * 14.0f + seedLane + 4.1f, 2);
		float n = base * 0.85f + grain * 0.15f;
		float t = std::clamp(n * 0.7f + 0.5f, 0.0f, 1.0f);
		Color3 color = Lerp(kDark, kLight, t);

		// ひび割れ: baseとは別レーン(+31.7)にしてブロッチの模様と重ならないようにする。
		float crack = CrackMask(su, sv, 5.0f, seedLane + 31.7f, 10.0f);
		return Lerp(color, kCrackColor, crack * 0.85f);
	};

	std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);
	for (uint32_t y = 0; y < size; ++y) {
		float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
		for (uint32_t x = 0; x < size; ++x) {
			float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);

			Color3 color = MakeTileable(u, v, colorAt);
			size_t pixelIndex = (static_cast<size_t>(y) * size + x) * 4;
			pixels[pixelIndex + 0] = ToByte(color.r);
			pixels[pixelIndex + 1] = ToByte(color.g);
			pixels[pixelIndex + 2] = ToByte(color.b);
			pixels[pixelIndex + 3] = 255;
		}
	}

	uint32_t textureIndex = TextureManager::GetInstance()->CreateTextureFromMemory(key, pixels.data(), size, size);
	GeneratedTextureCache()[key] = textureIndex;
	return textureIndex;
}

uint32_t GenerateMossyRockTexture(uint32_t size, float seedLane) {
	std::string key = "Generated/MossyRock/" + std::to_string(size) + "/" + std::to_string(seedLane);
	if (auto cached = GeneratedTextureCache().find(key); cached != GeneratedTextureCache().end()) {
		return cached->second;
	}

	// 岩肌をベースに、**窪んだ所だけへ苔を溜める**。
	// 一様に緑を混ぜると苔ではなく「緑に塗った石」にしかならない。苔らしさは
	// 「高い所は剥き出しの岩・低い所は緑」という分布そのものから出る。
	constexpr Color3 kRockDark = {0.13f, 0.12f, 0.11f};
	constexpr Color3 kRockLight = {0.52f, 0.50f, 0.46f};
	constexpr Color3 kMossDeep = {0.05f, 0.15f, 0.05f};
	constexpr Color3 kMossLight = {0.26f, 0.44f, 0.14f};

	auto colorAt = [&](float su, float sv) -> Color3 {
		float base = FractalNoise(su * 4.0f + seedLane, sv * 4.0f + seedLane, 4);
		float grain = FractalNoise(su * 19.0f + seedLane + 6.2f, sv * 19.0f + seedLane + 6.2f, 2);
		float n = base * 0.75f + grain * 0.25f;
		float t = std::clamp(n * 0.65f + 0.5f, 0.0f, 1.0f);
		Color3 rock = Lerp(kRockDark, kRockLight, t);

		// 窪み = 岩が暗い所。そこへ、別レーンの大きなパッチで「苔が生えている範囲」を掛ける
		// (岩全面が均一に苔むしていると、濡れた緑の塊になってしまう)。
		float cavity = std::clamp(1.0f - t, 0.0f, 1.0f);
		float patch = FractalNoise(su * 2.6f + seedLane + 51.3f, sv * 2.6f + seedLane + 51.3f, 3);
		float patchMask = std::clamp(patch * 0.9f + 0.5f, 0.0f, 1.0f);
		float moss = std::pow(std::clamp(cavity * 1.5f * patchMask, 0.0f, 1.0f), 1.3f);

		// 苔自体にも濃淡を入れる。単色だと塗りつぶしに見える。
		float mossShade = std::clamp(FractalNoise(su * 12.0f + seedLane + 88.1f, sv * 12.0f + seedLane + 88.1f, 3) * 0.6f + 0.5f, 0.0f, 1.0f);
		return Lerp(rock, Lerp(kMossDeep, kMossLight, mossShade), moss);
	};

	std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);
	for (uint32_t y = 0; y < size; ++y) {
		float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
		for (uint32_t x = 0; x < size; ++x) {
			float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);

			Color3 color = MakeTileable(u, v, colorAt);
			size_t pixelIndex = (static_cast<size_t>(y) * size + x) * 4;
			pixels[pixelIndex + 0] = ToByte(color.r);
			pixels[pixelIndex + 1] = ToByte(color.g);
			pixels[pixelIndex + 2] = ToByte(color.b);
			pixels[pixelIndex + 3] = 255;
		}
	}

	uint32_t textureIndex = TextureManager::GetInstance()->CreateTextureFromMemory(key, pixels.data(), size, size);
	GeneratedTextureCache()[key] = textureIndex;
	return textureIndex;
}

uint32_t GenerateSandTexture(uint32_t size, float seedLane) {
	std::string key = "Generated/Sand/" + std::to_string(size) + "/" + std::to_string(seedLane);
	if (auto cached = GeneratedTextureCache().find(key); cached != GeneratedTextureCache().end()) {
		return cached->second;
	}

	constexpr Color3 kDark = {0.34f, 0.26f, 0.15f};
	constexpr Color3 kLight = {0.74f, 0.62f, 0.41f};

	// 風紋の本数。**必ず整数にする。** MakeTileableはv-1でも評価するので、
	// 整数でないとsinの位相がずれてタイルの継ぎ目に段差が出る。
	constexpr float kRippleCount = 18.0f;
	constexpr float kTwoPi = 6.28318530718f;

	auto colorAt = [&](float su, float sv) -> Color3 {
		// まっすぐな縞は人工物に見えるので、低周波ノイズで縞そのものを曲げる。
		float warp = FractalNoise(su * 2.0f + seedLane, sv * 2.0f + seedLane, 3);
		float ripple = std::sin((sv * kRippleCount + warp * 2.2f) * kTwoPi) * 0.5f + 0.5f;
		// 砂粒。細かい高周波を薄く乗せる。
		float grain = FractalNoise(su * 42.0f + seedLane + 13.7f, sv * 42.0f + seedLane + 13.7f, 2);
		// 大きなうねり(砂丘)。
		float dune = FractalNoise(su * 1.6f + seedLane + 27.4f, sv * 1.6f + seedLane + 27.4f, 3);

		float t = std::clamp(0.5f + dune * 0.35f + (ripple - 0.5f) * 0.28f + grain * 0.14f, 0.0f, 1.0f);
		return Lerp(kDark, kLight, t);
	};

	std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);
	for (uint32_t y = 0; y < size; ++y) {
		float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
		for (uint32_t x = 0; x < size; ++x) {
			float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);

			Color3 color = MakeTileable(u, v, colorAt);
			size_t pixelIndex = (static_cast<size_t>(y) * size + x) * 4;
			pixels[pixelIndex + 0] = ToByte(color.r);
			pixels[pixelIndex + 1] = ToByte(color.g);
			pixels[pixelIndex + 2] = ToByte(color.b);
			pixels[pixelIndex + 3] = 255;
		}
	}

	uint32_t textureIndex = TextureManager::GetInstance()->CreateTextureFromMemory(key, pixels.data(), size, size);
	GeneratedTextureCache()[key] = textureIndex;
	return textureIndex;
}

uint32_t GenerateRustedMetalTexture(uint32_t size, float seedLane) {
	std::string key = "Generated/RustedMetal/" + std::to_string(size) + "/" + std::to_string(seedLane);
	if (auto cached = GeneratedTextureCache().find(key); cached != GeneratedTextureCache().end()) {
		return cached->second;
	}

	constexpr Color3 kIronDark = {0.10f, 0.10f, 0.12f};
	constexpr Color3 kIronLight = {0.36f, 0.36f, 0.39f};
	constexpr Color3 kRustDeep = {0.26f, 0.10f, 0.04f};
	constexpr Color3 kRustLight = {0.64f, 0.31f, 0.10f};

	auto colorAt = [&](float su, float sv) -> Color3 {
		float plate = FractalNoise(su * 6.0f + seedLane, sv * 6.0f + seedLane, 3);
		Color3 iron = Lerp(kIronDark, kIronLight, std::clamp(plate * 0.5f + 0.5f, 0.0f, 1.0f));

		// 錆は「塊」と、そこから**下へ流れた筋**でできる。
		// 筋はvの周波数だけ落として引き伸ばすことで作る(縦に長い模様になる)。
		float blotch = FractalNoise(su * 3.4f + seedLane + 19.8f, sv * 3.4f + seedLane + 19.8f, 4);
		float streak = FractalNoise(su * 16.0f + seedLane + 63.5f, sv * 1.8f + seedLane + 63.5f, 3);
		float rust = std::clamp(blotch * 0.75f + streak * 0.4f + 0.42f, 0.0f, 1.0f);
		rust = std::pow(rust, 1.9f);

		float rustShade = std::clamp(FractalNoise(su * 22.0f + seedLane + 5.5f, sv * 22.0f + seedLane + 5.5f, 2) * 0.7f + 0.5f, 0.0f, 1.0f);
		return Lerp(iron, Lerp(kRustDeep, kRustLight, rustShade), rust);
	};

	std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);
	for (uint32_t y = 0; y < size; ++y) {
		float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
		for (uint32_t x = 0; x < size; ++x) {
			float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);

			Color3 color = MakeTileable(u, v, colorAt);
			size_t pixelIndex = (static_cast<size_t>(y) * size + x) * 4;
			pixels[pixelIndex + 0] = ToByte(color.r);
			pixels[pixelIndex + 1] = ToByte(color.g);
			pixels[pixelIndex + 2] = ToByte(color.b);
			pixels[pixelIndex + 3] = 255;
		}
	}

	uint32_t textureIndex = TextureManager::GetInstance()->CreateTextureFromMemory(key, pixels.data(), size, size);
	GeneratedTextureCache()[key] = textureIndex;
	return textureIndex;
}

uint32_t GenerateDustSpriteTexture(uint32_t size, float seedLane) {
	std::string key = "Generated/DustSprite/" + std::to_string(size) + "/" + std::to_string(seedLane);
	if (auto cached = GeneratedTextureCache().find(key); cached != GeneratedTextureCache().end()) {
		return cached->second;
	}

	// 中心が濃く外周でゼロへ落ちる円形マスクに、fBmで輪郭を崩してもやついた見た目にする。
	constexpr Color3 kDustColor = {0.55f, 0.48f, 0.38f};

	std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);
	for (uint32_t y = 0; y < size; ++y) {
		float v = ((static_cast<float>(y) + 0.5f) / static_cast<float>(size)) * 2.0f - 1.0f;
		for (uint32_t x = 0; x < size; ++x) {
			float u = ((static_cast<float>(x) + 0.5f) / static_cast<float>(size)) * 2.0f - 1.0f;

			float dist = std::sqrt(u * u + v * v);
			float radialMask = std::clamp(1.0f - dist, 0.0f, 1.0f);
			radialMask *= radialMask;

			float noise = FractalNoise(u * 3.0f + seedLane, v * 3.0f + seedLane, 3);
			float wispy = std::clamp(noise * 0.5f + 0.5f, 0.0f, 1.0f);

			float alpha = std::clamp(radialMask * (0.4f + wispy * 0.6f), 0.0f, 1.0f);

			size_t pixelIndex = (static_cast<size_t>(y) * size + x) * 4;
			pixels[pixelIndex + 0] = ToByte(kDustColor.r);
			pixels[pixelIndex + 1] = ToByte(kDustColor.g);
			pixels[pixelIndex + 2] = ToByte(kDustColor.b);
			pixels[pixelIndex + 3] = ToByte(alpha);
		}
	}

	uint32_t textureIndex = TextureManager::GetInstance()->CreateTextureFromMemory(key, pixels.data(), size, size);
	GeneratedTextureCache()[key] = textureIndex;
	return textureIndex;
}


uint32_t GenerateMistTexture(uint32_t size, float seedLane, float frequency, float contrast) {
	std::string key = "Generated/Mist/" + std::to_string(size) + "/" + std::to_string(seedLane) + "/" + std::to_string(frequency) + "/" + std::to_string(contrast);
	if (auto cached = GeneratedTextureCache().find(key); cached != GeneratedTextureCache().end()) {
		return cached->second;
	}

	// αだけにfBmを載せ、RGBは白のままにする。
	// こうしておくと Image の Color がそのまま「靄の色と濃さ」になり、配色を変えても作り直さなくてよい。
	std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);
	for (uint32_t y = 0; y < size; ++y) {
		float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
		for (uint32_t x = 0; x < size; ++x) {
			float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);

			// 4隅ブレンドで周期化する。スクロールさせる用途なので継ぎ目が出ると一発で分かる。
			auto noiseAt = [&](float su, float sv) {
				return FractalNoise(su * frequency + seedLane, sv * frequency + seedLane, 4);
			};
			float n00 = noiseAt(u, v);
			float n10 = noiseAt(u - 1.0f, v);
			float n01 = noiseAt(u, v - 1.0f);
			float n11 = noiseAt(u - 1.0f, v - 1.0f);
			float top = n00 + (n10 - n00) * u;
			float bottom = n01 + (n11 - n01) * u;
			float noise = top + (bottom - top) * v;

			// -0.8..0.8 を 0..1 へ均して、べき乗で濃淡を締める(薄い所をより薄く)。
			float density = std::clamp(noise * 0.625f + 0.5f, 0.0f, 1.0f);
			float alpha = std::pow(density, contrast);

			size_t pixelIndex = (static_cast<size_t>(y) * size + x) * 4;
			pixels[pixelIndex + 0] = 255;
			pixels[pixelIndex + 1] = 255;
			pixels[pixelIndex + 2] = 255;
			pixels[pixelIndex + 3] = ToByte(alpha);
		}
	}

	uint32_t textureIndex = TextureManager::GetInstance()->CreateTextureFromMemory(key, pixels.data(), size, size);
	GeneratedTextureCache()[key] = textureIndex;
	return textureIndex;
}

uint32_t GenerateFogSheetTexture(uint32_t size, float seedLane, float frequency, float contrast) {
	std::string key =
	    "Generated/FogSheet/" + std::to_string(size) + "/" + std::to_string(seedLane) + "/" + std::to_string(frequency) + "/" + std::to_string(contrast);
	if (auto cached = GeneratedTextureCache().find(key); cached != GeneratedTextureCache().end()) {
		return cached->second;
	}

	// **濃さをRGBに入れ、αは常に255にする。** Object3dのPixelShaderは
	// `textureColor.a <= 0.5` でdiscardする(アルファテスト)ので、
	// **MistTextureのようなα勾配のテクスチャをモデルに貼ると、霧ではなく穴だらけの型抜きになる。**
	// 加算合成(BlendMode kAdd)で「黒=見えない・白=濃い」として使えば、その分岐に一切触れずに済む。
	//
	// uとvで周波数を変えて、横方向に細かく・縦方向に伸びた**縦筋**にする(霧の扉の質感)。
	const float frequencyU = frequency * 1.7f;
	const float frequencyV = frequency * 0.45f;

	std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);
	for (uint32_t y = 0; y < size; ++y) {
		float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
		for (uint32_t x = 0; x < size; ++x) {
			float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);

			// 4隅ブレンドで周期化する。UVを流す用途なので継ぎ目が出ると一発で分かる。
			auto noiseAt = [&](float su, float sv) {
				float coarse = FractalNoise(su * frequencyU + seedLane, sv * frequencyV + seedLane, 4);
				float fine = FractalNoise(su * frequencyU * 2.6f + seedLane + 11.7f, sv * frequencyV * 2.6f + seedLane + 11.7f, 2);
				return coarse * 0.6f + fine * 0.4f;
			};
			float n00 = noiseAt(u, v);
			float n10 = noiseAt(u - 1.0f, v);
			float n01 = noiseAt(u, v - 1.0f);
			float n11 = noiseAt(u - 1.0f, v - 1.0f);
			float top = n00 + (n10 - n00) * u;
			float bottom = n01 + (n11 - n01) * u;
			float noise = top + (bottom - top) * v;

			// -0.8..0.8 を 0..1 へ均す。ただし**4隅ブレンド(周期化)で振れ幅が半分近くまで潰れる**ので、
			// そのまま使うと0.3..0.7あたりに固まり、全面が均一に光る「光る壁」になってしまう。
			float density = std::clamp(noise * 0.625f + 0.5f, 0.0f, 1.0f);
			// 下限を切って薄い所を完全に抜き、残りを0..1へ引き伸ばす。ここで初めて「隙間のある霧」になる。
			constexpr float kFloor = 0.42f;
			density = std::clamp((density - kFloor) / (1.0f - kFloor), 0.0f, 1.0f);
			density = std::pow(density, contrast);

			uint8_t level = ToByte(density);
			size_t pixelIndex = (static_cast<size_t>(y) * size + x) * 4;
			pixels[pixelIndex + 0] = level;
			pixels[pixelIndex + 1] = level;
			pixels[pixelIndex + 2] = level;
			pixels[pixelIndex + 3] = 255;
		}
	}

	uint32_t textureIndex = TextureManager::GetInstance()->CreateTextureFromMemory(key, pixels.data(), size, size);
	GeneratedTextureCache()[key] = textureIndex;
	return textureIndex;
}

uint32_t GenerateFlameSpriteTexture(uint32_t size, float seedLane) {
	std::string key = "Generated/FlameSprite/" + std::to_string(size) + "/" + std::to_string(seedLane);
	if (auto cached = GeneratedTextureCache().find(key); cached != GeneratedTextureCache().end()) {
		return cached->second;
	}

	// **RGBは白のまま**にして、色はParticleSystemのStart/End Colorに任せる。
	// こうしておくと同じテクスチャで炎にも魔法の粒にも使い回せる。
	std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);
	for (uint32_t y = 0; y < size; ++y) {
		float v = ((static_cast<float>(y) + 0.5f) / static_cast<float>(size)) * 2.0f - 1.0f;
		for (uint32_t x = 0; x < size; ++x) {
			float u = ((static_cast<float>(x) + 0.5f) / static_cast<float>(size)) * 2.0f - 1.0f;

			// 中心を強く残した放射状の減衰。土埃(2乗)より鋭くして芯のある粒にする。
			float dist = std::sqrt(u * u + v * v);
			float core = std::clamp(1.0f - dist, 0.0f, 1.0f);
			core = core * core * core;

			// 輪郭に舌先のような揺らぎを足す。0.55..1.0に収めて、輪郭だけを崩し芯は消さない。
			float noise = FractalNoise(u * 3.5f + seedLane, v * 3.5f + seedLane, 3);
			float wisp = std::clamp(noise * 0.45f + 0.775f, 0.0f, 1.0f);

			float alpha = std::clamp(core * wisp, 0.0f, 1.0f);

			size_t pixelIndex = (static_cast<size_t>(y) * size + x) * 4;
			pixels[pixelIndex + 0] = 255;
			pixels[pixelIndex + 1] = 255;
			pixels[pixelIndex + 2] = 255;
			pixels[pixelIndex + 3] = ToByte(alpha);
		}
	}

	uint32_t textureIndex = TextureManager::GetInstance()->CreateTextureFromMemory(key, pixels.data(), size, size);
	GeneratedTextureCache()[key] = textureIndex;
	return textureIndex;
}
} // namespace NoiseTexture

} // namespace KujataEngine
