#pragma once
#include "../runtime/KujataApi.h"

#include <cmath>

namespace KujataEngine {

namespace EaseUtil {

enum class EaseType { Linear, InQuad, OutQuad, InOutQuad, InBack, OutBack, OutBounce };

KUJATA_API float GetEaseCalc(float t, EaseType easeType);

inline float EaseLerp(const float start, const float end, float t, EaseType type) {
	float e = GetEaseCalc(t, type);
	return static_cast<float>(static_cast<float>(start) + static_cast<float>(end - start) * e);
}

} // namespace Easing
} // namespace KujataEngine