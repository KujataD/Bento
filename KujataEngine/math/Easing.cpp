#include "Easing.h"

namespace KujataEngine {
namespace EaseUtil {

float GetEaseCalc(float t, EaseType easeType) {
	switch (easeType) {
	case EaseType::Linear: {
		return t;
		break;
	}
	case EaseType::InQuad: {
		return t * t;
		break;
	}
	case EaseType::OutQuad: {
		return t * (2 - t);
		break;
	}
	case EaseType::InOutQuad: {
		return t < 0.5f ? 2.0f * t * t : 1.0f - powf(-2.0f * t + 2.0f, 2.0f) / 2.0f;
		break;
	}
	case EaseType::InBack: {
		const float c1 = 1.70158f;
		const float c3 = c1 + 1.0f;

		return c3 * t * t * t - c1 * t * t;
		break;
	}
	case EaseType::OutBack: {
		const float c1 = 1.70158f;
		const float c3 = c1 + 1.0f;

		return 1.0f + c3 * powf(t - 1.0f, 3.0f) + c1 * powf(t - 1.0f, 2.0f);
		break;
	}
	case EaseType::OutBounce: {
		const float n1 = 7.5625f;
		const float d1 = 2.75f;

		if (t < 1.0f / d1) {
			return n1 * t * t;
		} else if (t < 2.0f / d1) {
			return n1 * (t -= 1.5f / d1) * t + 0.75f;
		} else if (t < 2.5f / d1) {
			return n1 * (t -= 2.25f / d1) * t + 0.9375f;
		} else {
			return n1 * (t -= 2.625f / d1) * t + 0.984375f;
		}
	}
	default: {
		return t;
		break;
	}
	}
}

} // namespace Easing
} // namespace KujataEngine