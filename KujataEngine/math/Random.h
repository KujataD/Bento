#pragma once
#include <numbers>
#include <random>
#include <type_traits>
#include <algorithm>

namespace KujataEngine {

class Random {
public:
	static void Initialize() {
		// 乱数生成エンジン
		std::random_device seedGenerator;
		// メルセンヌ·ツイスターエンジンの初期化
		randomEngine.seed(seedGenerator());
	}

	template<typename T> static T GetRandom(T min, T max) {
		// スワップ
		if (min > max) {
			std::swap(min, max);
		}

		// 整数型か少数型かどうか
		if constexpr (std::is_integral_v<T>) {
			std::uniform_int_distribution<T> distribution(min, max);
			return distribution(randomEngine);
		} else if constexpr (std::is_floating_point_v<T>) {
			std::uniform_real_distribution<T> distribution(min, max);
			return distribution(randomEngine);
		} else {
			return min;
		}
	}

private:
	// メルセンヌ·ツイスターエンジン(64bit版)
	static std::mt19937_64 randomEngine;
};

} // namespace KujataEngine